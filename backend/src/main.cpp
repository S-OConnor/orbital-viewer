// main.cpp — olv_backend entry point. Wires config -> logger -> StateStore ->
// InputSource (thread 1, chosen by cfg.input_mode) -> WsServer (thread 2,
// the main io_context).

#include <utility>  // std::exchange, needed before Boost.Asio on Boost 1.74

#include <boost/asio.hpp>

#include <chrono>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>

#include "config.hpp"
#include "input_source.hpp"
#include "logger.hpp"
#include "state_store.hpp"
#include "ws_server.hpp"

int main(int argc, char** argv) {
  std::string error;
  const auto cfg_opt = olv::parseArgs(argc, argv, error);
  if (!cfg_opt) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    olv::printUsage(argv[0]);
    return 2;
  }
  const olv::Config cfg = *cfg_opt;
  if (cfg.show_help) {
    olv::printUsage(argv[0]);
    return 0;
  }

  olv::Logger log;
  if (!log.open(cfg.log_file, cfg.log_level, cfg.log_stderr)) {
    std::fprintf(stderr, "error: cannot open log file: %s\n", cfg.log_file.c_str());
    return 1;
  }

  char hz[16];
  std::snprintf(hz, sizeof(hz), "%.2f", cfg.broadcast_hz);
  std::string startup = "startup udp_bind=" + cfg.udp_bind +
                        " udp_port=" + std::to_string(cfg.udp_port) + " ws_bind=" + cfg.ws_bind +
                        " ws_port=" + std::to_string(cfg.ws_port) +
                        " log_level=" + olv::toString(cfg.log_level) +
                        " expiry_seconds=" + std::to_string(cfg.expiry_seconds) +
                        " broadcast_hz=" + hz + " log_file=" + cfg.log_file;
  if (!cfg.config_file.empty()) startup += " config_file=" + cfg.config_file;
  // Only logged when non-default so a default run's log output stays
  // byte-identical to pre-InputSource builds (FEATURE_INPUT_SOURCES.md §7
  // Phase 1 regression guard).
  if (cfg.input_mode != olv::InputMode::kOlv1) {
    startup += std::string(" input_mode=") + olv::toString(cfg.input_mode);
  }
  log.info("main", startup);

  try {
    olv::StateStore store(std::chrono::seconds{cfg.expiry_seconds});
    std::unique_ptr<olv::InputSource> input = olv::makeInputSource(cfg, store, log);
    boost::asio::io_context ioc;
    olv::WsServer ws(ioc, cfg.ws_bind, cfg.ws_port, store, log, cfg.broadcast_hz);

    input->start();
    ws.start();

    boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code&, int) {
      log.info("main", "shutting down");
      ws.stop();
      input->stop();
      ioc.stop();
    });

    ioc.run();
    log.info("main", "clean shutdown");
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    log.error("main", std::string("fatal: ") + e.what());
    return 1;
  }
}
