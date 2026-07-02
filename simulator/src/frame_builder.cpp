// frame_builder.cpp — see frame_builder.hpp.

#include "frame_builder.hpp"

#include <algorithm>
#include <map>
#include <utility>

namespace olv::sim {

namespace {

struct Group {
  double t = 0.0;
  bool has_sat = false;
  proto::ObjectRecord sat;
  std::vector<proto::ObjectRecord> objects;
};

}  // namespace

GroupResult buildFrames(const std::vector<CsvRow>& rows) {
  GroupResult result;

  // std::map keeps groups ordered by time_s ascending; rows sharing the same
  // time_s literal (parsed from identical or coincidentally-equal text)
  // fall into the same group.
  std::map<double, Group> by_time;
  for (const CsvRow& row : rows) {
    Group& g = by_time[row.time_s];
    g.t = row.time_s;
    if (row.is_sat) {
      g.sat = row.rec;
      g.has_sat = true;
    } else {
      g.objects.push_back(row.rec);
    }
  }

  bool have_sat = false;
  proto::ObjectRecord last_sat;
  bool first = true;
  for (auto& [t, g] : by_time) {
    if (first) {
      first = false;
      if (!g.has_sat) {
        result.error = BuildError{"first frame (t=" + std::to_string(t) + ") has no sat row"};
        return result;
      }
    }
    if (g.has_sat) {
      last_sat = g.sat;
      have_sat = true;
    }

    Frame frame;
    frame.t = g.t;
    frame.sat = have_sat ? last_sat : proto::ObjectRecord{};
    frame.objects = std::move(g.objects);
    result.frames.push_back(std::move(frame));
  }

  return result;
}

std::vector<std::vector<std::uint8_t>> buildPackets(const Frame& frame, std::size_t chunk,
                                                    std::uint32_t& seq) {
  const std::size_t clamped_chunk = std::clamp<std::size_t>(chunk, 1, proto::kMaxObjectsPerPacket);
  const std::size_t total = frame.objects.size();
  const std::uint16_t object_total =
      static_cast<std::uint16_t>(std::min<std::size_t>(total, proto::kMaxTrackedObjects));

  std::vector<std::vector<std::uint8_t>> packets;
  std::size_t i = 0;
  do {
    const std::size_t end = std::min(i + clamped_chunk, total);

    proto::StatePacket pkt;
    pkt.sequence = seq++;
    pkt.sat_id = frame.sat.id;
    pkt.sat_px = frame.sat.px;
    pkt.sat_py = frame.sat.py;
    pkt.sat_pz = frame.sat.pz;
    pkt.sat_vx = frame.sat.vx;
    pkt.sat_vy = frame.sat.vy;
    pkt.sat_vz = frame.sat.vz;
    pkt.object_total = object_total;
    pkt.objects.assign(frame.objects.begin() + static_cast<std::ptrdiff_t>(i),
                       frame.objects.begin() + static_cast<std::ptrdiff_t>(end));

    packets.push_back(proto::encode(pkt));
    i = end;
  } while (i < total);

  return packets;
}

}  // namespace olv::sim
