// test_frame_builder.cpp — unit tests for frame_builder.hpp. Links into the
// same olv_sim_tests binary as test_csv_reader.cpp (which hosts
// OLV_TEST_MAIN()).

#include "frame_builder.hpp"

#include <cstdint>
#include <vector>

#include "csv_reader.hpp"
#include "olv/protocol.hpp"
#include "olv_test.hpp"

namespace {

olv::sim::CsvRow makeSatRow(double t, std::uint32_t id, double px, double py, double pz) {
  olv::sim::CsvRow row;
  row.time_s = t;
  row.is_sat = true;
  row.rec.id = id;
  row.rec.px = px;
  row.rec.py = py;
  row.rec.pz = pz;
  return row;
}

olv::sim::CsvRow makeObjRow(double t, std::uint32_t id, std::uint8_t type) {
  olv::sim::CsvRow row;
  row.time_s = t;
  row.is_sat = false;
  row.rec.id = id;
  row.rec.type = type;
  return row;
}

}  // namespace

OLV_TEST(frame_builder_groups_by_time) {
  std::vector<olv::sim::CsvRow> rows = {
      makeSatRow(0, 1, 100, 0, 0), makeObjRow(0, 10, 1), makeSatRow(1, 1, 200, 0, 0),
      makeObjRow(1, 11, 2),        makeObjRow(1, 12, 3),
  };

  olv::sim::GroupResult g = olv::sim::buildFrames(rows);
  OLV_CHECK(static_cast<bool>(g));
  OLV_CHECK_EQ(g.frames.size(), std::size_t{2});
  if (g.frames.size() != 2) return;

  OLV_CHECK_NEAR(g.frames[0].t, 0.0, 1e-9);
  OLV_CHECK_EQ(g.frames[0].objects.size(), std::size_t{1});
  OLV_CHECK_NEAR(g.frames[1].t, 1.0, 1e-9);
  OLV_CHECK_EQ(g.frames[1].objects.size(), std::size_t{2});
}

OLV_TEST(frame_builder_carries_forward_satellite) {
  std::vector<olv::sim::CsvRow> rows = {
      makeSatRow(0, 7, 111, 222, 333),
      makeObjRow(1, 20, 1),  // no sat row at t=1
      makeObjRow(2, 21, 1),  // nor at t=2
  };

  olv::sim::GroupResult g = olv::sim::buildFrames(rows);
  OLV_CHECK(static_cast<bool>(g));
  OLV_CHECK_EQ(g.frames.size(), std::size_t{3});
  if (g.frames.size() != 3) return;

  for (const olv::sim::Frame& f : g.frames) {
    OLV_CHECK_EQ(f.sat.id, 7u);
    OLV_CHECK_NEAR(f.sat.px, 111.0, 1e-9);
    OLV_CHECK_NEAR(f.sat.py, 222.0, 1e-9);
    OLV_CHECK_NEAR(f.sat.pz, 333.0, 1e-9);
  }
}

OLV_TEST(frame_builder_errors_without_leading_sat) {
  std::vector<olv::sim::CsvRow> rows = {makeObjRow(0, 1, 1)};
  olv::sim::GroupResult g = olv::sim::buildFrames(rows);
  OLV_CHECK(!static_cast<bool>(g));
  OLV_CHECK(g.error.has_value());
}

OLV_TEST(frame_builder_later_missing_sat_is_fine_if_first_has_one) {
  std::vector<olv::sim::CsvRow> rows = {
      makeSatRow(0, 1, 0, 0, 0),
      makeObjRow(1, 5, 1),  // t=1 has no sat row; must not error (carried forward)
  };
  olv::sim::GroupResult g = olv::sim::buildFrames(rows);
  OLV_CHECK(static_cast<bool>(g));
}

OLV_TEST(frame_builder_chunking_sizes_and_totals) {
  olv::sim::Frame frame;
  frame.t = 5.0;
  frame.sat.id = 1;
  frame.sat.px = 1.0;
  frame.sat.py = 2.0;
  frame.sat.pz = 3.0;
  frame.sat.vx = 0.1f;
  frame.sat.vy = 0.2f;
  frame.sat.vz = 0.3f;

  frame.objects.resize(300);
  for (std::size_t i = 0; i < frame.objects.size(); ++i) {
    olv::proto::ObjectRecord& o = frame.objects[i];
    o.id = static_cast<std::uint32_t>(i + 1);
    o.type = static_cast<std::uint8_t>(olv::proto::ObjectType::kDebris);
    o.confidence = 50;
    o.px = static_cast<double>(i);
    o.py = static_cast<double>(i) * 2.0;
    o.pz = static_cast<double>(i) * 3.0;
  }

  std::uint32_t seq = 0;
  std::vector<std::vector<std::uint8_t>> packets = olv::sim::buildPackets(frame, 128, seq);
  OLV_CHECK_EQ(packets.size(), std::size_t{3});
  OLV_CHECK_EQ(seq, 3u);
  if (packets.size() != 3) return;

  OLV_CHECK_EQ(packets[0].size(), std::size_t{56 + 128 * 48 + 4});
  OLV_CHECK_EQ(packets[1].size(), std::size_t{56 + 128 * 48 + 4});
  OLV_CHECK_EQ(packets[2].size(), std::size_t{56 + 44 * 48 + 4});

  // Round-trip: decode every packet and verify sequence continuity,
  // object_total, satellite state, and object content against the inputs.
  std::uint32_t expected_seq = 0;
  std::size_t obj_index = 0;
  for (const std::vector<std::uint8_t>& raw : packets) {
    olv::proto::StatePacket pkt;
    olv::proto::DecodeError err = olv::proto::decode(raw.data(), raw.size(), pkt);
    OLV_CHECK(err == olv::proto::DecodeError::kNone);
    OLV_CHECK_EQ(pkt.sequence, expected_seq);
    ++expected_seq;
    OLV_CHECK_EQ(pkt.object_total, static_cast<std::uint16_t>(300));
    OLV_CHECK_EQ(pkt.sat_id, 1u);
    OLV_CHECK_NEAR(pkt.sat_px, 1.0, 1e-9);
    OLV_CHECK_NEAR(pkt.sat_py, 2.0, 1e-9);
    OLV_CHECK_NEAR(pkt.sat_pz, 3.0, 1e-9);
    OLV_CHECK_NEAR(pkt.sat_vx, 0.1, 1e-6);

    for (const olv::proto::ObjectRecord& rec : pkt.objects) {
      OLV_CHECK(obj_index < frame.objects.size());
      if (obj_index < frame.objects.size()) {
        OLV_CHECK_EQ(rec.id, frame.objects[obj_index].id);
        OLV_CHECK_EQ(rec.type, frame.objects[obj_index].type);
        OLV_CHECK_NEAR(rec.px, frame.objects[obj_index].px, 1e-9);
        OLV_CHECK_NEAR(rec.py, frame.objects[obj_index].py, 1e-9);
        OLV_CHECK_NEAR(rec.pz, frame.objects[obj_index].pz, 1e-9);
      }
      ++obj_index;
    }
  }
  OLV_CHECK_EQ(obj_index, std::size_t{300});
}

OLV_TEST(frame_builder_seq_continuity_across_frames) {
  olv::sim::Frame f1;
  f1.t = 0.0;
  f1.sat.id = 1;
  f1.objects.resize(200);
  for (olv::proto::ObjectRecord& o : f1.objects) o.type = 1;

  olv::sim::Frame f2;
  f2.t = 1.0;
  f2.sat.id = 1;
  f2.objects.resize(50);
  for (olv::proto::ObjectRecord& o : f2.objects) o.type = 1;

  std::uint32_t seq = 0;
  std::vector<std::vector<std::uint8_t>> p1 = olv::sim::buildPackets(f1, 128, seq);
  OLV_CHECK_EQ(p1.size(), std::size_t{2});  // 200 objects -> 128 + 72
  OLV_CHECK_EQ(seq, 2u);

  std::vector<std::vector<std::uint8_t>> p2 = olv::sim::buildPackets(f2, 128, seq);
  OLV_CHECK_EQ(p2.size(), std::size_t{1});
  OLV_CHECK_EQ(seq, 3u);
  if (p2.empty()) return;

  olv::proto::StatePacket pkt;
  olv::proto::DecodeError err = olv::proto::decode(p2[0].data(), p2[0].size(), pkt);
  OLV_CHECK(err == olv::proto::DecodeError::kNone);
  OLV_CHECK_EQ(pkt.sequence, 2u);
  OLV_CHECK_EQ(pkt.object_total, static_cast<std::uint16_t>(50));
}

OLV_TEST(frame_builder_zero_objects_still_emits_one_packet) {
  olv::sim::Frame f;
  f.t = 0.0;
  f.sat.id = 9;

  std::uint32_t seq = 0;
  std::vector<std::vector<std::uint8_t>> packets = olv::sim::buildPackets(f, 128, seq);
  OLV_CHECK_EQ(packets.size(), std::size_t{1});
  OLV_CHECK_EQ(seq, 1u);
  if (packets.empty()) return;
  OLV_CHECK_EQ(packets[0].size(), std::size_t{56 + 4});

  olv::proto::StatePacket pkt;
  olv::proto::DecodeError err = olv::proto::decode(packets[0].data(), packets[0].size(), pkt);
  OLV_CHECK(err == olv::proto::DecodeError::kNone);
  OLV_CHECK_EQ(pkt.object_total, static_cast<std::uint16_t>(0));
  OLV_CHECK_EQ(pkt.sat_id, 9u);
}

OLV_TEST(frame_builder_chunk_is_clamped) {
  olv::sim::Frame f;
  f.t = 0.0;
  f.sat.id = 1;
  f.objects.resize(5);
  for (olv::proto::ObjectRecord& o : f.objects) o.type = 1;

  std::uint32_t seq = 0;
  std::vector<std::vector<std::uint8_t>> lo = olv::sim::buildPackets(f, 0, seq);  // clamp to 1
  OLV_CHECK_EQ(lo.size(), std::size_t{5});

  seq = 0;
  std::vector<std::vector<std::uint8_t>> hi =
      olv::sim::buildPackets(f, 999999, seq);  // clamp to 128
  OLV_CHECK_EQ(hi.size(), std::size_t{1});
}
