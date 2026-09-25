// ws_probe.cpp — standalone WebSocket capture client for the integration test.
//
// Connects to ws://<host>:<port>/, reads text frames until it has captured
// --count of them (each must contain a JSON `"type":"..."` field), appending
// one frame per line to --out. A global deadline is enforced by driving the
// whole async chain with io_context::run_for(--timeout). Links only Boost +
// Threads (no olv_core).
//
//   ws_probe --host H --port P --count N --timeout SECONDS --out FILE

#include <utility>  // std::exchange, needed before Boost.Asio/Beast on Boost 1.74

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = boost::beast::websocket;
using tcp = boost::asio::ip::tcp;

namespace {

struct Probe {
  asio::io_context ioc;
  tcp::resolver resolver{ioc};
  websocket::stream<tcp::socket> ws{ioc};
  beast::flat_buffer buffer;
  std::string host;
  std::string port;
  std::string out_path;
  int target = 0;
  int captured = 0;
  bool ok = false;
  std::string err;
  std::ofstream out;

  void start() {
    out.open(out_path, std::ios::out | std::ios::trunc);
    if (!out) {
      err = "cannot open output file: " + out_path;
      return;
    }
    resolver.async_resolve(host, port, [this](beast::error_code ec, tcp::resolver::results_type r) {
      if (ec) return fail("resolve", ec);
      asio::async_connect(ws.next_layer(), r, [this](beast::error_code ec2, const tcp::endpoint&) {
        if (ec2) return fail("connect", ec2);
        ws.async_handshake(host, "/", [this](beast::error_code ec3) {
          if (ec3) return fail("handshake", ec3);
          readNext();
        });
      });
    });
  }

  void readNext() {
    ws.async_read(buffer, [this](beast::error_code ec, std::size_t) {
      if (ec) return fail("read", ec);
      const std::string frame = beast::buffers_to_string(buffer.data());
      buffer.consume(buffer.size());
      if (frame.find("\"type\":\"") == std::string::npos) {
        err = "frame missing type field";
        ioc.stop();
        return;
      }
      out << frame << '\n';
      out.flush();
      if (++captured >= target) {
        ok = true;
        ioc.stop();
        return;
      }
      readNext();
    });
  }

  void fail(const char* stage, beast::error_code ec) {
    err = std::string(stage) + " failed: " + ec.message();
    ioc.stop();
  }
};

const char* nextValue(int& i, int argc, char** argv, const char* name) {
  if (i + 1 >= argc) {
    std::fprintf(stderr, "ws_probe: missing value for %s\n", name);
    std::exit(1);
  }
  return argv[++i];
}

}  // namespace

int main(int argc, char** argv) {
  std::string host;
  std::string out;
  int port = 0;
  int count = 0;
  int timeout = 0;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--host") {
      host = nextValue(i, argc, argv, "--host");
    } else if (a == "--port") {
      port = std::atoi(nextValue(i, argc, argv, "--port"));
    } else if (a == "--count") {
      count = std::atoi(nextValue(i, argc, argv, "--count"));
    } else if (a == "--timeout") {
      timeout = std::atoi(nextValue(i, argc, argv, "--timeout"));
    } else if (a == "--out") {
      out = nextValue(i, argc, argv, "--out");
    } else {
      std::fprintf(stderr, "ws_probe: unknown flag: %s\n", a.c_str());
      return 1;
    }
  }

  if (host.empty() || out.empty() || port <= 0 || port > 65535 || count <= 0 || timeout <= 0) {
    std::fprintf(stderr,
                 "ws_probe: usage: --host H --port P --count N --timeout SECONDS --out FILE\n");
    return 1;
  }

  Probe probe;
  probe.host = host;
  probe.port = std::to_string(port);
  probe.out_path = out;
  probe.target = count;
  probe.start();
  probe.ioc.run_for(std::chrono::seconds(timeout));

  if (probe.ok) {
    std::printf("ws_probe: captured %d frames\n", probe.captured);
    return 0;
  }
  if (probe.err.empty()) probe.err = "timeout";
  std::fprintf(stderr, "ws_probe: %s (captured %d of %d)\n", probe.err.c_str(), probe.captured,
               probe.target);
  return 1;
}
