// frame_builder.hpp — group parsed CSV rows (or generator output) into
// per-instant frames, and encode a frame into OLV1 UDP packets.
//
// A Frame is everything known at one simulation instant: the primary
// satellite state plus the full list of tracked objects. `buildFrames()`
// implements the CSV carry-forward rule from docs/PLAN.md: the first time
// group must contain a "sat" row (rejected otherwise), and any later time
// group that omits one simply keeps the previous satellite state, since the
// wire protocol always repeats the satellite in every packet.
//
// `buildPackets()` is the only place StatePacket/proto::encode are used by
// the simulator: it chunks a frame's objects into <= `chunk` records per
// packet (protocol max 128), and every emitted packet independently repeats
// the satellite state and the frame's full object_total (see
// docs/PROTOCOL_UDP.md §2 — "each packet is self-contained").

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "csv_reader.hpp"
#include "olv/protocol.hpp"

namespace olv::sim {

struct Frame {
  double t = 0.0;
  proto::ObjectRecord sat;  // pos/vel/id valid; type/flags/confidence/intensity unused
  std::vector<proto::ObjectRecord> objects;
};

struct BuildError {
  std::string message;
};

struct GroupResult {
  std::vector<Frame> frames;  // ascending order of t
  std::optional<BuildError> error;

  explicit operator bool() const { return !error.has_value(); }
};

// Groups `rows` by time_s into ascending-order frames. Rows may be given in
// any order. Fails if the earliest time group has no "sat" row; every later
// group without one carries the most recently seen satellite state forward.
GroupResult buildFrames(const std::vector<CsvRow>& rows);

// Encodes one frame into 1..N wire packets (always at least one, even with
// zero objects, so the satellite state keeps flowing). `chunk` is clamped to
// [1, kMaxObjectsPerPacket]. `seq` is the caller-owned running sequence
// counter: it is incremented once per packet emitted here, so callers should
// share one `seq` across an entire run (including across frames/cycles) to
// get a single monotonic per-packet sequence as required by the protocol.
std::vector<std::vector<std::uint8_t>> buildPackets(const Frame& frame, std::size_t chunk,
                                                    std::uint32_t& seq);

}  // namespace olv::sim
