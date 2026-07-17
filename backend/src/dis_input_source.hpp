// dis_input_source.hpp — thread: DIS Entity State PDU receive loop over UDP
// (the "dis" InputSource; see docs/FEATURE_INPUT_SOURCES.md §5).
//
// Same threading shape as Olv1InputSource: a private io_context run on an
// internal std::thread. Each datagram: StateStore::countReceived -> parse the
// 12-byte DIS PDU header -> route on protocolVersion / exerciseID / pduType ->
// if it is an Entity State PDU (kind 1), decode via the vendored open-dis-cpp
// library -> map to a StateStore-compatible StatePacket -> StateStore::apply.
// A datagram too short for the header, a header with an out-of-range
// protocolVersion, or a body open-dis cannot unmarshal is counted via
// StateStore::countDropped(DropKind::kMalformed) and logged, exactly like
// Olv1InputSource's malformed-packet path. Recognized-but-unsupported PDU kinds
// and exercise-filtered PDUs are counted in udp_received only (§3.2, §3.5).
//
// The concrete rules implemented here are the frozen §3 decisions: Entity State
// PDUs only (§3.2), FNV-1a-32 EntityID fold and satellite_entity_id match
// (§3.3), the EntityType table with a kDebris fallback and fixed
// confidence/intensity/flags (§3.4), the optional exercise_id filter (§3.5),
// and the per-entity DIS-timestamp staleness rule plus satellite carry-forward /
// synthesized-sequence calling convention into StateStore::apply (§3.6).

#pragma once

#include <utility>  // std::exchange, needed before Boost.Asio on Boost 1.74

#include <boost/asio.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include "input_source.hpp"
#include "olv/dis_entity_id.hpp"  // parseDisEntityId — shared with the simulator
#include "state_store.hpp"

namespace olv {

class Logger;

// Consulted only when Config::input_mode == InputMode::kDis (§6). A distinct
// default port from OLV1's 47000 so both modes' defaults can coexist on a host.
struct DisInputConfig {
  std::string bind_address = "0.0.0.0";
  std::uint16_t port = 47001;
  std::optional<std::uint8_t> exercise_id;  // [0,255] filter; unset = accept all
  std::string satellite_entity_id;          // "site:application:entity", required
};

class DisInputSource : public InputSource {
 public:
  // Binds cfg.bind_address:cfg.port immediately (throws
  // boost::system::system_error on a bad address or bind failure, so startup
  // errors surface before threads exist), mirroring Olv1InputSource. Throws
  // std::invalid_argument if cfg.satellite_entity_id is not a valid
  // "site:application:entity" (config validation normally rejects that first).
  DisInputSource(const DisInputConfig& cfg, StateStore& store, Logger& log);
  ~DisInputSource() override;

  DisInputSource(const DisInputSource&) = delete;
  DisInputSource& operator=(const DisInputSource&) = delete;

  void start() override;  // spawns the receive thread; no-op if already started
  void stop() override;   // stops the io_context and joins; idempotent

  // Runs one datagram through the full DIS translation exactly as the receive
  // loop does. Exposed so unit tests exercise decode/mapping/staleness/filtering
  // without a live socket (mirroring how the OLV1 path is tested via
  // proto::decode). `from` is used only in log lines.
  void processDatagram(const std::uint8_t* data, std::size_t len, const std::string& from);

 private:
  void armReceive();

  // Per-entity DIS-timestamp staleness (§3.6). Returns true (and does not
  // update state) when `ts` is stale for `packed`; otherwise records `ts` as
  // the entity's newest and returns false.
  bool isStale(std::uint64_t packed, std::uint32_t ts);

  DisInputConfig cfg_;
  std::uint16_t sat_site_ = 0, sat_app_ = 0, sat_entity_ = 0;  // parsed satellite id

  boost::asio::io_context ioc_;
  boost::asio::ip::udp::socket socket_;
  boost::asio::ip::udp::endpoint sender_;
  // Sized for the largest UDP payload; DIS Entity State PDUs vary in length
  // (optional articulation parameters), and we only read the base fields.
  std::array<std::uint8_t, 65536> buffer_{};
  StateStore& store_;
  Logger& log_;
  std::thread thread_;
  bool started_ = false;

  // Per-entity newest accepted DIS timestamp, keyed by the packed 48-bit
  // EntityID (site<<32 | application<<16 | entity). Includes the satellite.
  std::unordered_map<std::uint64_t, std::uint32_t> last_ts_;
  // folded 32-bit id -> packed 48-bit EntityID, for non-satellite entities.
  // Serves two purposes: the fold-collision check (§3.3, a folded id claimed by
  // a different EntityID) and the distinct-object count fed to object_total.
  std::unordered_map<std::uint32_t, std::uint64_t> object_fold_;
  // Last-known satellite fields, stamped into every synthesized StatePacket so
  // StateStore::apply's wholesale satellite replace carries it forward (§3.6).
  SatelliteState sat_{};
  // Process-local strictly-increasing sequence so apply's OLV1 staleness check
  // never fires in DIS mode (§3.6); starts at 1 (pre-increment from 0).
  std::uint32_t seq_ = 0;
};

}  // namespace olv
