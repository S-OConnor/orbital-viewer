// olv2_builder.hpp — batch per-instant frames into OLV2 per-track datagrams
// (docs/features/FEATURE_OLV2.md §7), the third encoding alongside OLV1
// (frame_builder.hpp) and DIS (dis_builder.hpp), selected by
// `[send] protocol = "olv2"`.
//
// OLV2 carries one target track per datagram with up to 25 time-stamped
// points, each holding the satellite (ownship) and target state at that
// instant. Batches are disjoint: every buffered frame contributes exactly one
// point to each object present in it, and flush() empties the buffer, so no
// point is ever sent twice.
//
// Mapping (per buffered frame, per object in that frame):
//  - Point::t = the frame's t_epoch (UTC seconds; caller-supplied).
//  - sat_pos/sat_vel from frame.sat; kPointSatHasVel is ALWAYS set (the
//    simulator's satellite always has a velocity, as in OLV1).
//  - tgt_pos/tgt_vel from the object record; kPointTgtHasVel iff the record
//    has kFlagHasVelocity (velocity sent as 0 otherwise).
//  - Header: track_id = object id, sat_id = frame.sat.id, target_type /
//    confidence from the object record in the NEWEST buffered frame that
//    contains it, target_flags = that record's HIGHLIGHT bit.
// Documented losses: object intensity (not on the OLV2 wire).
//
// Pure std (no Boost, no sockets), so it lives in olv_sim_lib and is
// unit-testable by decoding the output with proto::olv2::decode.

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "frame_builder.hpp"
#include "olv/protocol_olv2.hpp"

namespace olv::sim {

class Olv2Batcher {
 public:
  // `points_per_datagram` is clamped to [1, proto::olv2::kMaxPoints].
  explicit Olv2Batcher(std::size_t points_per_datagram);

  // Buffers one frame sampled at UTC epoch time `t_epoch`. Callers MUST pass
  // strictly increasing t_epoch across the whole run (use a cycle counter,
  // not frame.t, so CSV --loop stays monotone).
  void push(const Frame& frame, double t_epoch);

  // True once points_per_datagram frames are buffered.
  bool ready() const;

  // True when no frames are buffered (nothing for flush() to emit).
  bool empty() const;

  // Emits one datagram per distinct object id across the buffered frames, in
  // ascending id order; each carries one point per buffered frame that
  // contains the object (ascending t). Objects absent from some frames just
  // carry fewer points. `seq` is incremented once per datagram emitted (shared
  // across the run, like buildPackets). Clears the buffer. Returns an empty
  // vector when empty().
  std::vector<std::vector<std::uint8_t>> flush(std::uint32_t& seq);

  std::size_t pointsPerDatagram() const { return points_per_datagram_; }

 private:
  std::size_t points_per_datagram_;
  std::vector<std::pair<double, Frame>> frames_;  // (t_epoch, frame), push order
};

}  // namespace olv::sim
