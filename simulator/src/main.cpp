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
#include "frame_builder.hpp"
#include "generator.hpp"
#include "olv/protocol.hpp"
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
  boost::asio::io_context ioc;
  boost::asio::ip::udp::socket socket(ioc);
  boost::system::error_code ec;
  const boost::asio::ip::address addr = boost::asio::ip::make_address(cfg.dest_host, ec);
  if (ec) {
    std::cerr << "error: --dest: invalid address: " << cfg.dest_host << "\n";
    return 1;
  }
  const boost::asio::ip::udp::endpoint endpoint(addr, cfg.dest_port);
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

  auto sendFrame = [&](const Frame& frame) {
    const std::vector<std::vector<std::uint8_t>> packets =
        olv::sim::buildPackets(frame, chunk, seq);
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

  std::cout << "done: cycles=" << cycles << " packets=" << packets_sent << " bytes=" << bytes_sent
            << " send_errors=" << send_errors << "\n";
  return 0;
}
