// main.cpp — olv_backend entry point. Wires config -> logger -> StateStore ->
// UdpReceiver (thread 1) -> WsServer (thread 2, the main io_context).

#include <boost/asio.hpp>

#include <chrono>
#include <cstdio>
#include <exception>
#include <string>

#include "config.hpp"
#include "logger.hpp"
#include "state_store.hpp"
#include "udp_receiver.hpp"
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
  log.info("main", startup);

  try {
    olv::StateStore store(std::chrono::seconds{cfg.expiry_seconds});
    olv::UdpReceiver udp(cfg.udp_bind, cfg.udp_port, store, log);
    boost::asio::io_context ioc;
    olv::WsServer ws(ioc, cfg.ws_bind, cfg.ws_port, store, log, cfg.broadcast_hz);

    udp.start();
    ws.start();

    boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code&, int) {
      log.info("main", "shutting down");
      ws.stop();
      udp.stop();
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
