// olv2_input_source.hpp — thread: OLV2 per-track batched UDP receive loop
// (the "olv2" InputSource; see docs/features/FEATURE_OLV2.md and the
// normative wire spec docs/PROTOCOL_OLV2.md).
//
// Same threading shape as Olv1InputSource/DisInputSource: a private
// io_context run on an internal std::thread. Each datagram:
// StateStore::countReceived -> proto::olv2::decode -> per-track staleness
// (§4 item 11) -> translate to a TrackUpdate -> StateStore::applyTrack.
//  - decode failure: StateStore::countDropped(kMalformed), logged at warn
//    ("dropped <reason> len=.. from=..", the OLV1 format).
//  - stale (points[0].t <= newest accepted t for that track_id):
//    StateStore::countDropped(kStale), logged at debug
//    ("dropped stale track=.. t=.. from=..").
//  - accepted: logged at debug ("accepted track=.. points=.. seq=.. from=..").
//
// Translation rules (FEATURE_OLV2.md §4, A3):
//  - target: the NEWEST point's tgt position/velocity, with type/confidence
//    from the header, flags = HIGHLIGHT from target_flags | HAS_VELOCITY iff
//    that point has kPointTgtHasVel, intensity 0.
//  - satellite: the NEWEST point's sat position; id = sat_id, seq = the
//    datagram sequence, satellite_t = that point's t. Velocity: the point's
//    sat velocity when kPointSatHasVel is set; otherwise, with >= 2 points,
//    the finite difference of the last two sat positions over their dt;
//    otherwise 0.
//  - trail: every point's (t, tgt position), ascending t, id = track_id.
//  - a sat_id different from the previously accepted one is logged once per
//    change at warn ("satellite id changed old=.. new=..") and still applied.

#pragma once

#include <utility>  // std::exchange, needed before Boost.Asio on Boost 1.74

#include <boost/asio.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include "olv/input_source.hpp"
#include "olv/protocol_olv2.hpp"
#include "olv/state_store.hpp"

namespace olv {

class Logger;

// Consulted only when Config::input_mode == InputMode::kOlv2. A distinct
// default port from OLV1's 47000 and DIS's 47001 so all three modes'
// defaults can coexist on a host.
struct Olv2InputConfig {
  std::string bind_address = "0.0.0.0";
  std::uint16_t port = 47002;
};

class Olv2InputSource : public InputSource {
 public:
  // Binds cfg.bind_address:cfg.port immediately (throws
  // boost::system::system_error on a bad address or bind failure, so startup
  // errors surface before threads exist), mirroring Olv1InputSource.
  Olv2InputSource(const Olv2InputConfig& cfg, StateStore& store, Logger& log);
  ~Olv2InputSource() override;

  Olv2InputSource(const Olv2InputSource&) = delete;
  Olv2InputSource& operator=(const Olv2InputSource&) = delete;

  void start() override;  // spawns the receive thread; no-op if already started
  void stop() override;   // stops the io_context and joins; idempotent

  // Runs one datagram through the full OLV2 path exactly as the receive loop
  // does (count, decode, staleness, translate, apply). Exposed so unit tests
  // drive it without a live socket, like DisInputSource::processDatagram.
  // `from` is used only in log lines.
  void processDatagram(const std::uint8_t* data, std::size_t len, const std::string& from);

 private:
  void armReceive();

  boost::asio::io_context ioc_;
  boost::asio::ip::udp::socket socket_;
  boost::asio::ip::udp::endpoint sender_;
  // One byte larger than the max valid packet so oversized datagrams are
  // observed as kTooLong instead of being silently truncated by the OS.
  std::array<std::uint8_t, proto::olv2::kMaxPacketSize + 1> buffer_{};
  StateStore& store_;
  Logger& log_;
  std::thread thread_;
  bool started_ = false;

  // Per-track newest accepted sample time (§4 item 11).
  std::unordered_map<std::uint32_t, double> last_t_;
  // sat_id of the last accepted datagram, for the change warning.
  std::optional<std::uint32_t> last_sat_id_;
};

}  // namespace olv
