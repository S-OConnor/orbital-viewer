// olv2_builder.cpp — see olv2_builder.hpp for the mapping contract.

#include "olv2_builder.hpp"

#include <algorithm>
#include <map>

namespace olv::sim {

namespace {

// Per-track accumulator built while walking the buffered frames in push
// order (ascending t_epoch): one Point per frame that contains the object,
// plus the object record and satellite id from the newest such frame (for
// the datagram header).
struct TrackAccumulator {
  std::vector<proto::olv2::Point> points;
  const proto::ObjectRecord* newest_record = nullptr;
  std::uint32_t newest_sat_id = 0;
};

}  // namespace

Olv2Batcher::Olv2Batcher(std::size_t points_per_datagram)
    : points_per_datagram_(
          std::clamp(points_per_datagram, std::size_t{1}, std::size_t{proto::olv2::kMaxPoints})) {}

void Olv2Batcher::push(const Frame& frame, double t_epoch) {
  frames_.emplace_back(t_epoch, frame);
}

bool Olv2Batcher::ready() const {
  return frames_.size() >= points_per_datagram_;
}

bool Olv2Batcher::empty() const {
  return frames_.empty();
}

std::vector<std::vector<std::uint8_t>> Olv2Batcher::flush(std::uint32_t& seq) {
  std::vector<std::vector<std::uint8_t>> datagrams;
  if (frames_.empty()) return datagrams;

  // Walking frames_ in push (ascending t_epoch) order means each object's
  // TrackAccumulator naturally accumulates points in ascending t, and
  // newest_record/newest_sat_id end up holding the last (newest) frame that
  // contained the object. std::map keeps object ids in ascending order.
  std::map<std::uint32_t, TrackAccumulator> tracks;
  for (const auto& [t_epoch, frame] : frames_) {
    for (const proto::ObjectRecord& rec : frame.objects) {
      TrackAccumulator& acc = tracks[rec.id];

      proto::olv2::Point point;
      point.t = t_epoch;
      point.flags = proto::olv2::kPointSatHasVel;
      point.sat_px = frame.sat.px;
      point.sat_py = frame.sat.py;
      point.sat_pz = frame.sat.pz;
      point.sat_vx = frame.sat.vx;
      point.sat_vy = frame.sat.vy;
      point.sat_vz = frame.sat.vz;
      point.tgt_px = rec.px;
      point.tgt_py = rec.py;
      point.tgt_pz = rec.pz;
      if (rec.hasVelocity()) {
        point.flags |= proto::olv2::kPointTgtHasVel;
        point.tgt_vx = rec.vx;
        point.tgt_vy = rec.vy;
        point.tgt_vz = rec.vz;
      }
      acc.points.push_back(point);
      acc.newest_record = &rec;
      acc.newest_sat_id = frame.sat.id;
    }
  }

  datagrams.reserve(tracks.size());
  for (const auto& [track_id, acc] : tracks) {
    proto::olv2::TrackPacket pkt;
    pkt.sequence = seq++;
    pkt.track_id = track_id;
    pkt.sat_id = acc.newest_sat_id;
    pkt.target_type = acc.newest_record->type;
    pkt.target_flags = static_cast<std::uint8_t>(acc.newest_record->flags & proto::kFlagHighlight);
    pkt.confidence = acc.newest_record->confidence;
    pkt.points = acc.points;
    datagrams.push_back(proto::olv2::encode(pkt));
  }

  frames_.clear();
  return datagrams;
}

}  // namespace olv::sim
