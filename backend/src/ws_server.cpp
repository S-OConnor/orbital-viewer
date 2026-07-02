// ws_server.cpp — see ws_server.hpp. Single-threaded Beast WebSocket server on
// the caller's io_context, plus the broadcast timer. All sessions and the
// sessions_ set are touched only on the io_context thread, so no locking here.

#include "ws_server.hpp"

#include <utility>  // std::exchange, needed before Boost.Beast on Boost 1.74

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <chrono>
#include <deque>
#include <memory>
#include <string>

#include "json_writer.hpp"
#include "logger.hpp"

namespace olv {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = boost::beast::websocket;
using tcp = boost::asio::ip::tcp;

// ---------------------------------------------------------------------------
// Session: one WebSocket connection. Sends hello, drains a per-session outbox,
// and discards inbound frames to keep the read loop alive for close detection.
// ---------------------------------------------------------------------------

class WsServer::Session : public std::enable_shared_from_this<Session> {
 public:
  Session(tcp::socket&& socket, WsServer& server, std::string endpoint)
      : ws_(std::move(socket)), server_(server), endpoint_(std::move(endpoint)) {}

  void run() {
    ws_.text(true);
    auto self = shared_from_this();
    ws_.async_accept([self](const boost::system::error_code& ec) { self->onAccept(ec); });
  }

  // Enqueue a shared frame. Bounded queue: oldest droppable frames are shed on
  // overflow (frames, not the connection). Safe to call while a write is in
  // flight — the in-flight front frame is preserved.
  void send(const std::shared_ptr<const std::string>& frame) {
    if (closed_) return;
    outbox_.push_back(frame);
    enforceQueueLimit();
    if (!writing_) doWrite();
  }

  // Server-initiated shutdown: close without logging a disconnect.
  void close() {
    if (closed_) return;
    closed_ = true;
    boost::system::error_code ec;
    ws_.next_layer().close(ec);
  }

 private:
  void onAccept(const boost::system::error_code& ec) {
    if (ec) return;  // handshake failed; never registered, session drops
    server_.sessions_.insert(shared_from_this());
    server_.log_.info("ws", "client connected " + endpoint_ +
                                " clients=" + std::to_string(server_.sessions_.size()));
    auto hello = std::make_shared<const std::string>(
        buildHelloMessage(std::chrono::system_clock::now(), server_.broadcast_hz_));
    send(hello);
    doRead();
  }

  void doRead() {
    auto self = shared_from_this();
    ws_.async_read(read_buffer_,
                   [self](const boost::system::error_code& ec, std::size_t) { self->onRead(ec); });
  }

  void onRead(const boost::system::error_code& ec) {
    if (ec) {
      fail();
      return;
    }
    read_buffer_.consume(read_buffer_.size());  // discard client frames
    doRead();
  }

  void doWrite() {
    writing_ = true;
    auto self = shared_from_this();
    ws_.async_write(asio::buffer(*outbox_.front()), [self](const boost::system::error_code& ec,
                                                           std::size_t) { self->onWrite(ec); });
  }

  void onWrite(const boost::system::error_code& ec) {
    writing_ = false;
    if (ec) {
      fail();
      return;
    }
    outbox_.pop_front();
    if (outbox_.empty()) {
      overflow_warned_ = false;  // burst ended; a later overflow may warn again
    } else {
      doWrite();
    }
  }

  void enforceQueueLimit() {
    // Preserve the in-flight front frame (referenced by async_write).
    const std::size_t protect = writing_ ? 1u : 0u;
    bool dropped = false;
    while (outbox_.size() > kMaxQueuedFrames) {
      outbox_.erase(outbox_.begin() + static_cast<std::ptrdiff_t>(protect));
      dropped = true;
    }
    if (dropped && !overflow_warned_) {
      overflow_warned_ = true;
      server_.log_.warn("ws", "outbox overflow, dropping frames for " + endpoint_);
    }
  }

  void fail() {
    if (closed_) return;
    closed_ = true;
    boost::system::error_code ec;
    ws_.next_layer().close(ec);
    if (server_.sessions_.erase(shared_from_this()) > 0) {
      server_.log_.info("ws", "client disconnected " + endpoint_ +
                                  " clients=" + std::to_string(server_.sessions_.size()));
    }
  }

  websocket::stream<tcp::socket> ws_;
  WsServer& server_;
  std::string endpoint_;
  beast::flat_buffer read_buffer_;
  std::deque<std::shared_ptr<const std::string>> outbox_;
  bool writing_ = false;
  bool closed_ = false;
  bool overflow_warned_ = false;
};

// ---------------------------------------------------------------------------
// WsServer
// ---------------------------------------------------------------------------

WsServer::WsServer(asio::io_context& ioc, const std::string& bind_address, std::uint16_t port,
                   StateStore& store, Logger& log, double broadcast_hz)
    : ioc_(ioc),
      acceptor_(ioc, tcp::endpoint(asio::ip::make_address(bind_address), port)),
      timer_(ioc),
      store_(store),
      log_(log),
      broadcast_hz_(broadcast_hz) {}

WsServer::~WsServer() {
  stop();
}

void WsServer::start() {
  doAccept();
  armTimer();
}

void WsServer::stop() {
  if (stopped_) return;
  stopped_ = true;
  boost::system::error_code ec;
  acceptor_.close(ec);
  timer_.cancel();
  auto sessions = sessions_;  // copy: close() erases from sessions_
  for (const auto& s : sessions) s->close();
  sessions_.clear();
}

int WsServer::clientCount() const {
  return static_cast<int>(sessions_.size());
}

void WsServer::doAccept() {
  acceptor_.async_accept([this](const boost::system::error_code& ec, tcp::socket socket) {
    if (ec == asio::error::operation_aborted) return;
    if (!ec) {
      boost::system::error_code epec;
      const auto rep = socket.remote_endpoint(epec);
      std::string endpoint = epec ? std::string("unknown")
                                  : rep.address().to_string() + ":" + std::to_string(rep.port());
      std::make_shared<Session>(std::move(socket), *this, std::move(endpoint))->run();
    }
    if (!stopped_) doAccept();
  });
}

void WsServer::armTimer() {
  if (stopped_) return;
  const auto period = std::chrono::milliseconds(static_cast<long long>(1000.0 / broadcast_hz_));
  timer_.expires_after(period);
  timer_.async_wait([this](const boost::system::error_code& ec) {
    if (ec) return;  // cancelled
    broadcastTick();
  });
}

void WsServer::broadcastTick() {
  if (stopped_) return;
  ++broadcast_seq_;
  Snapshot snap = store_.snapshot(std::chrono::steady_clock::now());
  snap.stats.broadcast_seq = broadcast_seq_;
  auto msg = std::make_shared<const std::string>(buildStateMessage(
      snap, std::chrono::system_clock::now(), static_cast<int>(sessions_.size())));
  for (const auto& s : sessions_) s->send(msg);
  armTimer();
}

}  // namespace olv
