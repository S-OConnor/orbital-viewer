// dis_builder.hpp — encode a frame as IEEE 1278.1 DIS Entity State PDUs
// (docs/FEATURE_INPUT_SOURCES.md Phase 3), the alternative to the OLV1
// encoding in frame_builder.hpp, selected by `[send] protocol = "dis"`.
//
// DIS has no packet-of-many-records shape: each entity is one Entity State
// PDU in its own datagram, so `buildDisPdus()` returns 1 (satellite) +
// frame.objects.size() buffers and the `chunk` setting does not apply.
//
// Mapping is the exact inverse of the backend's frozen decode rules
// (docs/FEATURE_INPUT_SOURCES.md §3), so a `--protocol dis` run against
// `olv_backend --input-mode dis` round-trips to the same StateStore contents
// as the equivalent OLV1 run, with these documented DIS-inherent losses:
//  - EntityID: the satellite is emitted as `satellite_entity_id` (matching
//    the backend's required `dis_satellite_entity_id`); each object id is
//    spread as site = cfg.site, application = id >> 16, entity = id & 0xFFFF
//    (lossless for the 32-bit OLV id space; the backend then folds it back to
//    a different-but-deterministic 32-bit id via FNV-1a per §3.3). An object
//    whose spread EntityID happens to equal the configured satellite id would
//    be decoded as the satellite — with the defaults ("1:1:1") that requires
//    object id 0x00010001; avoid such ids in CSV missions.
//  - EntityType carries only the OLV object type: satellite → (kind 1, domain
//    5), ground_hot → (1, 1), comet → (2, 0), debris → (0, 0). STAR has no
//    row in the backend's frozen §3.4 table, so stars are emitted as (0, 0)
//    and decode as debris; per-object confidence/intensity are likewise not
//    representable in DIS (the backend fixes them at 100 / 0.0).
//  - Timestamp: DIS time-of-hour ticks derived from `t_seconds` (31-bit tick
//    field, LSB flag 0), so a monotonically increasing caller time keeps the
//    backend's per-entity staleness rule (§3.6) accepting every PDU, hourly
//    wraparound included.
//
// Pure std + the open-dis-cpp marshalling library (no Boost, no sockets), so
// it lives in olv_sim_lib and is unit-testable without the network.

#pragma once

#include <cstdint>
#include <vector>

#include "frame_builder.hpp"

namespace olv::sim {

struct DisEmitConfig {
  std::uint8_t exercise_id = 1;  // stamped into every PDU header
  std::uint16_t site = 1;        // EntityID site for all emitted objects
  // Satellite EntityID, "site:application:entity" — must match the backend's
  // [input] dis_satellite_entity_id for the satellite to be recognized.
  std::uint16_t sat_site = 1, sat_application = 1, sat_entity = 1;
};

// Encodes `frame` at caller time `t_seconds` (seconds since the run started;
// must be non-decreasing across calls) into one Entity State PDU per entity:
// the satellite first, then frame.objects in order. Each returned buffer is
// one complete datagram payload (144-byte base PDU, no articulation
// parameters). Deterministic: a pure function of (frame, cfg, t_seconds).
std::vector<std::vector<std::uint8_t>> buildDisPdus(const Frame& frame, const DisEmitConfig& cfg,
                                                    double t_seconds);

}  // namespace olv::sim
