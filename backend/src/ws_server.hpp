// ws_server.hpp — thread 2: Boost.Beast WebSocket server + 1 Hz broadcaster.
//
// Runs entirely on the caller-provided io_context (which main() runs on the
// main thread). All sessions live on that single thread, so no session-level
// locking is needed; the only cross-thread touch point is
// StateStore::snapshot(), which locks internally.
//
// Behavior:
//  - accepts WebSocket upgrades on 0.0.0.0:<port>, any path, text frames only
//  - sends a `hello` message (json_writer) immediately after the handshake
//  - a steady_timer fires at broadcast_hz: takes a snapshot, sets
//    stats.broadcast_seq, serializes ONCE into a shared_ptr<const string>,
//    and enqueues it to every open session
//  - per-session outbox: writes are chained (one async_write in flight); if a
//    slow client's queue exceeds kMaxQueuedFrames the oldest frames are
//    dropped (frames, not the connection) and a kWarn is logged, rate-limited
//  - client->server frames are read and discarded (keeps the read loop alive
//    to detect close/errors)
//  - connect/disconnect (with endpoint) logged at kInfo

#pragma once

#include <boost/asio.hpp>

#include <cstdint>
#include <memory>
#include <set>

#include "state_store.hpp"

namespace olv {

class Logger;

class WsServer {
 public:
  static constexpr std::size_t kMaxQueuedFrames = 5;

  // Binds/listens on <bind_address>:<port> immediately (throws
  // boost::system::system_error on a bad address or bind failure). Does not
  // start accepting/broadcasting until start(). bind_address is a dotted
  // IPv4/IPv6 literal, e.g. "0.0.0.0".
  WsServer(boost::asio::io_context& ioc, const std::string& bind_address, std::uint16_t port,
           StateStore& store, Logger& log, double broadcast_hz = 1.0);
  ~WsServer();

  WsServer(const WsServer&) = delete;
  WsServer& operator=(const WsServer&) = delete;

  void start();  // begin accept loop + broadcast timer
  void stop();   // close acceptor, cancel timer, close all sessions

  int clientCount() const;

 private:
  class Session;  // defined in ws_server.cpp

  void doAccept();
  void armTimer();
  void broadcastTick();

  boost::asio::io_context& ioc_;
  boost::asio::ip::tcp::acceptor acceptor_;
  boost::asio::steady_timer timer_;
  StateStore& store_;
  Logger& log_;
  double broadcast_hz_;
  std::uint64_t broadcast_seq_ = 0;
  std::set<std::shared_ptr<Session>> sessions_;  // touched only on the ioc thread
  bool stopped_ = false;
};

}  // namespace olv
