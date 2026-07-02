// udp_receiver.hpp — thread 1: UDP receive loop.
//
// Owns a private io_context run on an internal std::thread. Each datagram:
// StateStore::countReceived -> proto::decode -> on success StateStore::apply,
// on failure StateStore::countDropped(kMalformed). Every packet (accepted or
// dropped) is logged: accepted at kDebug ("seq=.. objs=.. total=.. from=.."),
// drops at kWarn with the DecodeError reason and source endpoint; stale
// sequences at kDebug (expected under packet reordering).

#pragma once

#include <boost/asio.hpp>

#include <array>
#include <cstdint>
#include <thread>

#include "olv/protocol.hpp"
#include "state_store.hpp"

namespace olv {

class Logger;

class UdpReceiver {
 public:
  // Binds <bind_address>:<port> immediately (throws boost::system::system_error
  // on a bad address or bind failure so startup errors surface before threads
  // exist). bind_address is a dotted IPv4/IPv6 literal, e.g. "0.0.0.0".
  UdpReceiver(const std::string& bind_address, std::uint16_t port, StateStore& store, Logger& log);
  ~UdpReceiver();

  UdpReceiver(const UdpReceiver&) = delete;
  UdpReceiver& operator=(const UdpReceiver&) = delete;

  void start();  // spawns the receive thread; no-op if already started
  void stop();   // stops the io_context and joins; idempotent

 private:
  void armReceive();
  void handleDatagram(std::size_t len);

  boost::asio::io_context ioc_;
  boost::asio::ip::udp::socket socket_;
  boost::asio::ip::udp::endpoint sender_;
  // One byte larger than the max valid packet so oversized datagrams are
  // observed as kTooLong instead of being silently truncated by the OS.
  std::array<std::uint8_t, proto::kMaxPacketSize + 1> buffer_{};
  StateStore& store_;
  Logger& log_;
  std::thread thread_;
  bool started_ = false;
};

}  // namespace olv
