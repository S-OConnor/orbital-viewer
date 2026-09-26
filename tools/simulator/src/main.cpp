// main.cpp — olv_sim CLI.
//
// Replays a CSV mission file (--csv) or synthesizes a deterministic
// load-test scenario (--generate N) as OLV1 UDP packets (docs/PROTOCOL_UDP.md)
// sent to --dest:--port at --rate cycles/second. This is the only simulator
// file that touches the network (Boost.Asio, synchronous UDP socket); CLI /
// TOML config parsing lives in sim_config.{hpp,cpp} and
// csv_reader/frame_builder/generator are pure std, so all of those are
// exercised directly by the unit tests without a socket.
//
// Shutdown: SIGINT/SIGTERM use the OS default handler (immediate process
// termination) rather than a custom asio::signal_set — acceptable for a
// dev/load-test tool per the spec. Each per-cycle status line is flushed
// immediately so output isn't lost if the process is signaled mid-run.

#include <utility>  // std::exchange, needed before Boost.Asio on Boost 1.74

#include <boost/asio.hpp>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "csv_reader.hpp"
#include "dis_builder.hpp"
#include "frame_builder.hpp"
#include "generator.hpp"
#include "olv/dis_entity_id.hpp"
#include "olv/protocol.hpp"
#include "olv2_builder.hpp"
#include "sim_config.hpp"

namespace {

using olv::sim::Frame;
using olv::sim::SimConfig;

}  // namespace

int main(int argc, char** argv) {
  std::string error;
  std::optional<SimConfig> parsed = olv::sim::parseSimArgs(argc, argv, error);
  if (!parsed) {
    std::cerr << "error: " << error << "\n";
    olv::sim::printSimUsage(argv[0]);
    return 1;
  }
  SimConfig cfg = *parsed;
  if (cfg.show_help) {
    olv::sim::printSimUsage(argv[0]);
    return 0;
  }

  if (!cfg.quiet && !cfg.config_file.empty()) {
    std::cout << "using config file: " << cfg.config_file << "\n";
  }

  // ---- Load frames (CSV) or prepare the generator. -----------------------
  std::vector<Frame> csv_frames;
  std::optional<olv::sim::Generator> generator;
  const bool csv_mode = cfg.mode == SimConfig::Mode::kCsv;

  if (csv_mode) {
    std::ifstream in(cfg.csv_path);
    if (!in) {
      std::cerr << "error: cannot open CSV file: " << cfg.csv_path << "\n";
      return 1;
    }
    olv::sim::ParseResult parsed_csv = olv::sim::parseCsv(in);
    if (!parsed_csv) {
      std::cerr << "error: " << cfg.csv_path << ":" << parsed_csv.error->line << ": "
                << parsed_csv.error->message << "\n";
      return 1;
    }
    olv::sim::GroupResult grouped = olv::sim::buildFrames(parsed_csv.rows);
    if (!grouped) {
      std::cerr << "error: " << cfg.csv_path << ": " << grouped.error->message << "\n";
      return 1;
    }
    csv_frames = std::move(grouped.frames);
    if (csv_frames.empty()) {
      std::cerr << "error: " << cfg.csv_path << ": no data rows\n";
      return 1;
    }
  } else {
    generator.emplace(static_cast<std::size_t>(cfg.generate_count), cfg.seed);
  }

  // ---- UDP socket (connected, so each cycle is a plain send()). ----------
  // Resolve --dest, which may be a numeric IP literal (e.g. 127.0.0.1) or a
  // hostname (e.g. a container/compose service name such as "backend"). Prefer
  // an IPv4 result since the backend binds 0.0.0.0 (IPv4) by default.
  boost::asio::io_context ioc;
  boost::asio::ip::udp::socket socket(ioc);
  boost::system::error_code ec;
  boost::asio::ip::udp::resolver resolver(ioc);
  const auto results = resolver.resolve(cfg.dest_host, std::to_string(cfg.dest_port), ec);
  if (ec || results.empty()) {
    std::cerr << "error: --dest: cannot resolve " << cfg.dest_host << ":" << cfg.dest_port;
    if (ec) std::cerr << ": " << ec.message();
    std::cerr << "\n";
    return 1;
  }
  boost::asio::ip::udp::endpoint endpoint = results.begin()->endpoint();
  for (const auto& entry : results) {
    if (entry.endpoint().address().is_v4()) {
      endpoint = entry.endpoint();
      break;
    }
  }
  socket.open(endpoint.protocol(), ec);
  if (!ec) socket.connect(endpoint, ec);
  if (ec) {
    std::cerr << "error: socket: " << ec.message() << "\n";
    return 1;
  }

  // ---- Send loop. ----------------------------------------------------------
  using Clock = std::chrono::steady_clock;
  const auto period =
      std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / cfg.rate_hz));
  const Clock::time_point start = Clock::now();
  Clock::time_point next_tick = start;

  std::uint32_t seq = 0;
  std::uint64_t cycles = 0;
  std::uint64_t packets_sent = 0;
  std::uint64_t bytes_sent = 0;
  std::uint64_t send_errors = 0;
  const std::size_t chunk = static_cast<std::size_t>(cfg.chunk);
  const bool dis_mode = cfg.protocol == SimConfig::Protocol::kDis;
  const bool olv2_mode = cfg.protocol == SimConfig::Protocol::kOlv2;

  // DIS emit settings (--protocol dis): validated by parseSimArgs, so the
  // entity-id parse here cannot fail.
  olv::sim::DisEmitConfig dis_cfg;
  dis_cfg.exercise_id = static_cast<std::uint8_t>(cfg.dis_exercise_id);
  dis_cfg.site = static_cast<std::uint16_t>(cfg.dis_site);
  olv::parseDisEntityId(cfg.dis_satellite_entity_id, dis_cfg.sat_site, dis_cfg.sat_application,
                        dis_cfg.sat_entity);

  // OLV2 emit settings (--protocol olv2): every frame's t_epoch is this
  // run-start wall-clock time plus cycles/rate_hz, computed from the cycle
  // counter (not frame.t) so CSV --loop keeps t_epoch monotone, matching the
  // DIS timestamp precedent above.
  std::optional<olv::sim::Olv2Batcher> olv2_batcher;
  const double start_epoch =
      std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
  if (olv2_mode) {
    olv2_batcher.emplace(static_cast<std::size_t>(cfg.olv2_points));
    const double batch_period_s = static_cast<double>(cfg.olv2_points) / cfg.rate_hz;
    if (batch_period_s > 10.0) {
      std::cerr << "warning: olv2 batch period " << batch_period_s
                << " s exceeds 10 s; the backend's object expiry (default 15 s) may drop "
                   "tracks between batches\n";
    }
  }

  auto sendPackets = [&](const std::vector<std::vector<std::uint8_t>>& packets) {
    for (const std::vector<std::uint8_t>& pkt : packets) {
      boost::system::error_code send_ec;
      socket.send(boost::asio::buffer(pkt), 0, send_ec);
      if (send_ec) {
        // Non-fatal: on a connected UDP socket, a prior datagram to a
        // port with no listener can surface here as an ICMP-triggered
        // error (e.g. "connection refused") on the *next* send. This is
        // routine for a fire-and-forget load-test sender (the receiver
        // may not have started yet), so log to stderr and keep going
        // rather than exiting the whole run. Unlike the per-cycle status
        // line, this is not suppressed by --quiet: it is a diagnostic
        // for real packet loss, not routine progress output.
        std::cerr << "warning: send: " << send_ec.message() << "\n";
        ++send_errors;
        continue;
      }
      ++packets_sent;
      bytes_sent += pkt.size();
    }
  };

  auto sendFrame = [&](const Frame& frame) {
    // DIS timestamps must be monotone across the whole run (including CSV
    // --loop wraps, where frame.t resets), so they derive from the cycle
    // counter, not from frame.t. OLV2 buffers frames and only emits
    // datagrams once its batch is ready(), so `packets` here may be empty.
    std::vector<std::vector<std::uint8_t>> packets;
    if (dis_mode) {
      packets = olv::sim::buildDisPdus(frame, dis_cfg, static_cast<double>(cycles) / cfg.rate_hz);
    } else if (olv2_mode) {
      const double t_epoch = start_epoch + static_cast<double>(cycles) / cfg.rate_hz;
      olv2_batcher->push(frame, t_epoch);
      if (olv2_batcher->ready()) packets = olv2_batcher->flush(seq);
    } else {
      packets = olv::sim::buildPackets(frame, chunk, seq);
    }
    sendPackets(packets);
    ++cycles;
    if (!cfg.quiet) {
      std::cout << "t=" << frame.t << " packets=" << packets.size()
                << " objects=" << frame.objects.size() << "\n";
      std::cout.flush();
    }
  };

  if (csv_mode) {
    std::size_t csv_index = 0;
    while (true) {
      if (csv_index >= csv_frames.size()) {
        if (!cfg.loop) break;
        csv_index = 0;
      }
      sendFrame(csv_frames[csv_index++]);
      next_tick += period;
      std::this_thread::sleep_until(next_tick);
    }
  } else {
    const double duration_s = static_cast<double>(cfg.duration_seconds);
    while (true) {
      const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
      if (duration_s > 0.0 && elapsed >= duration_s) break;
      sendFrame(generator->frameAt(elapsed));
      next_tick += period;
      std::this_thread::sleep_until(next_tick);
    }
  }

  // Normal end of run (duration elapsed, or CSV end without --loop): flush
  // and send any partial OLV2 batch rather than discarding its buffered
  // points. Never reached when the run only stops via a signal.
  if (olv2_mode && !olv2_batcher->empty()) {
    sendPackets(olv2_batcher->flush(seq));
  }

  std::cout << "done: cycles=" << cycles << " packets=" << packets_sent << " bytes=" << bytes_sent
            << " send_errors=" << send_errors << "\n";
  return 0;
}
