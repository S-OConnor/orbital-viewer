// test_olv2_builder.cpp — Olv2Batcher, verified by decoding its output with
// proto::olv2::decode. OLV_TEST_MAIN() lives in test_csv_reader.cpp; this
// file links into the same olv_sim_tests binary.

#include "olv2_builder.hpp"

#include <cstdint>
#include <vector>

#include "olv/protocol.hpp"
#include "olv/protocol_olv2.hpp"
#include "olv_test.hpp"

namespace {

namespace olv2 = olv::proto::olv2;
using olv::proto::ObjectRecord;
using olv::proto::ObjectType;
using olv::sim::Frame;
using olv::sim::Olv2Batcher;

ObjectRecord makeObject(std::uint32_t id, bool has_velocity, ObjectType type = ObjectType::kDebris,
                        std::uint8_t confidence = 50, bool highlight = false) {
  ObjectRecord r;
  r.id = id;
  r.type = static_cast<std::uint8_t>(type);
  r.confidence = confidence;
  r.flags = static_cast<std::uint8_t>((has_velocity ? olv::proto::kFlagHasVelocity : 0) |
                                      (highlight ? olv::proto::kFlagHighlight : 0));
  r.px = 1.0e6;
  r.py = 2.0e6;
  r.pz = 3.0e6;
  r.vx = 10.0f;
  r.vy = 20.0f;
  r.vz = 30.0f;
  return r;
}

Frame makeFrame(std::uint32_t sat_id = 1) {
  Frame f;
  f.sat.id = sat_id;
  f.sat.px = 7.0e6;
  f.sat.py = 8.0e5;
  f.sat.pz = -9.0e5;
  f.sat.vx = 7.5f;
  f.sat.vy = -1.5f;
  f.sat.vz = 0.25f;
  return f;
}

olv2::TrackPacket decodeOne(const std::vector<std::uint8_t>& buf) {
  olv2::TrackPacket pkt;
  const olv2::DecodeError err = olv2::decode(buf.data(), buf.size(), pkt);
  OLV_CHECK(err == olv2::DecodeError::kNone);
  return pkt;
}

}  // namespace

OLV_TEST(olv2_batcher_clamps_points_per_datagram) {
  OLV_CHECK_EQ(Olv2Batcher(0).pointsPerDatagram(), std::size_t{1});
  OLV_CHECK_EQ(Olv2Batcher(1).pointsPerDatagram(), std::size_t{1});
  OLV_CHECK_EQ(Olv2Batcher(25).pointsPerDatagram(), std::size_t{25});
  OLV_CHECK_EQ(Olv2Batcher(26).pointsPerDatagram(), std::size_t{25});
  OLV_CHECK_EQ(Olv2Batcher(1000).pointsPerDatagram(), std::size_t{25});
}

OLV_TEST(olv2_batcher_ready_and_empty_transitions) {
  Olv2Batcher b(3);
  OLV_CHECK(b.empty());
  OLV_CHECK(!b.ready());

  Frame f = makeFrame();
  f.objects.push_back(makeObject(1, true));

  b.push(f, 100.0);
  OLV_CHECK(!b.empty());
  OLV_CHECK(!b.ready());

  b.push(f, 101.0);
  OLV_CHECK(!b.empty());
  OLV_CHECK(!b.ready());

  b.push(f, 102.0);
  OLV_CHECK(b.ready());

  std::uint32_t seq = 0;
  const auto datagrams = b.flush(seq);
  OLV_CHECK_EQ(datagrams.size(), std::size_t{1});
  OLV_CHECK(b.empty());
  OLV_CHECK(!b.ready());
}

OLV_TEST(olv2_batcher_flush_on_empty_returns_nothing) {
  Olv2Batcher b(5);
  std::uint32_t seq = 0;
  const auto datagrams = b.flush(seq);
  OLV_CHECK(datagrams.empty());
  OLV_CHECK_EQ(seq, std::uint32_t{0});
}

OLV_TEST(olv2_batcher_one_datagram_per_object_ascending_id) {
  Olv2Batcher b(2);
  Frame f1 = makeFrame();
  f1.objects.push_back(makeObject(30, true));
  f1.objects.push_back(makeObject(10, true));
  f1.objects.push_back(makeObject(20, true));
  Frame f2 = makeFrame();
  f2.objects.push_back(makeObject(30, true));
  f2.objects.push_back(makeObject(10, true));
  f2.objects.push_back(makeObject(20, true));

  b.push(f1, 10.0);
  b.push(f2, 11.0);
  OLV_CHECK(b.ready());

  std::uint32_t seq = 0;
  const auto datagrams = b.flush(seq);
  OLV_CHECK_EQ(datagrams.size(), std::size_t{3});

  std::uint32_t last_id = 0;
  for (std::size_t i = 0; i < datagrams.size(); ++i) {
    olv2::TrackPacket pkt = decodeOne(datagrams[i]);
    if (i > 0) OLV_CHECK(pkt.track_id > last_id);
    last_id = pkt.track_id;
  }
  OLV_CHECK_EQ(last_id, std::uint32_t{30});
}

OLV_TEST(olv2_batcher_point_count_matches_frames_containing_object) {
  Olv2Batcher b(3);
  Frame f1 = makeFrame();
  f1.objects.push_back(makeObject(1, true));
  f1.objects.push_back(makeObject(2, true));
  Frame f2 = makeFrame();
  f2.objects.push_back(makeObject(1, true));  // object 2 absent from this frame
  Frame f3 = makeFrame();
  f3.objects.push_back(makeObject(1, true));
  f3.objects.push_back(makeObject(2, true));

  b.push(f1, 1.0);
  b.push(f2, 2.0);
  b.push(f3, 3.0);
  OLV_CHECK(b.ready());

  std::uint32_t seq = 0;
  const auto datagrams = b.flush(seq);
  OLV_CHECK_EQ(datagrams.size(), std::size_t{2});

  olv2::TrackPacket pkt1 = decodeOne(datagrams[0]);
  OLV_CHECK_EQ(pkt1.track_id, std::uint32_t{1});
  OLV_CHECK_EQ(pkt1.points.size(), std::size_t{3});

  olv2::TrackPacket pkt2 = decodeOne(datagrams[1]);
  OLV_CHECK_EQ(pkt2.track_id, std::uint32_t{2});
  OLV_CHECK_EQ(pkt2.points.size(), std::size_t{2});
}

OLV_TEST(olv2_batcher_point_times_ascending_and_match_pushed_t_epoch) {
  Olv2Batcher b(3);
  Frame f = makeFrame();
  f.objects.push_back(makeObject(1, true));

  b.push(f, 5.0);
  b.push(f, 5.5);
  b.push(f, 6.25);

  std::uint32_t seq = 0;
  const auto datagrams = b.flush(seq);
  olv2::TrackPacket pkt = decodeOne(datagrams[0]);
  OLV_CHECK_EQ(pkt.points.size(), std::size_t{3});
  OLV_CHECK_NEAR(pkt.points[0].t, 5.0, 1e-9);
  OLV_CHECK_NEAR(pkt.points[1].t, 5.5, 1e-9);
  OLV_CHECK_NEAR(pkt.points[2].t, 6.25, 1e-9);
}

OLV_TEST(olv2_batcher_sat_flag_always_set_tgt_flag_per_record) {
  Olv2Batcher b(2);
  Frame f1 = makeFrame();
  f1.objects.push_back(makeObject(1, false));  // no target velocity
  Frame f2 = makeFrame();
  f2.objects.push_back(makeObject(1, true));  // has target velocity

  b.push(f1, 1.0);
  b.push(f2, 2.0);

  std::uint32_t seq = 0;
  const auto datagrams = b.flush(seq);
  olv2::TrackPacket pkt = decodeOne(datagrams[0]);
  OLV_CHECK_EQ(pkt.points.size(), std::size_t{2});

  OLV_CHECK(pkt.points[0].satHasVelocity());
  OLV_CHECK(!pkt.points[0].tgtHasVelocity());
  OLV_CHECK_NEAR(pkt.points[0].tgt_vx, 0.0, 1e-9);
  OLV_CHECK_NEAR(pkt.points[0].tgt_vy, 0.0, 1e-9);
  OLV_CHECK_NEAR(pkt.points[0].tgt_vz, 0.0, 1e-9);

  OLV_CHECK(pkt.points[1].satHasVelocity());
  OLV_CHECK(pkt.points[1].tgtHasVelocity());
  OLV_CHECK_NEAR(pkt.points[1].tgt_vx, 10.0, 1e-6);
}

OLV_TEST(olv2_batcher_header_fields_from_newest_frame_and_highlight_bit) {
  Olv2Batcher b(2);
  Frame f1 = makeFrame(1);
  f1.objects.push_back(makeObject(1, true, ObjectType::kDebris, 40, false));
  Frame f2 = makeFrame(2);
  f2.objects.push_back(makeObject(1, true, ObjectType::kComet, 77, true));

  b.push(f1, 1.0);
  b.push(f2, 2.0);

  std::uint32_t seq = 0;
  const auto datagrams = b.flush(seq);
  olv2::TrackPacket pkt = decodeOne(datagrams[0]);
  OLV_CHECK_EQ(pkt.sat_id, std::uint32_t{2});  // newest frame's satellite id
  OLV_CHECK_EQ(pkt.target_type, static_cast<std::uint8_t>(ObjectType::kComet));
  OLV_CHECK_EQ(pkt.confidence, std::uint8_t{77});
  OLV_CHECK_EQ(pkt.target_flags, olv::proto::kFlagHighlight);
}

OLV_TEST(olv2_batcher_seq_increments_across_datagrams_and_flushes) {
  Olv2Batcher b(1);
  Frame f = makeFrame();
  f.objects.push_back(makeObject(1, true));
  f.objects.push_back(makeObject(2, true));

  b.push(f, 1.0);
  std::uint32_t seq = 0;
  const auto first = b.flush(seq);
  OLV_CHECK_EQ(first.size(), std::size_t{2});
  OLV_CHECK_EQ(seq, std::uint32_t{2});

  olv2::TrackPacket p0 = decodeOne(first[0]);
  olv2::TrackPacket p1 = decodeOne(first[1]);
  OLV_CHECK_EQ(p0.sequence, std::uint32_t{0});
  OLV_CHECK_EQ(p1.sequence, std::uint32_t{1});

  b.push(f, 2.0);
  const auto second = b.flush(seq);
  OLV_CHECK_EQ(second.size(), std::size_t{2});
  OLV_CHECK_EQ(seq, std::uint32_t{4});
  olv2::TrackPacket p2 = decodeOne(second[0]);
  OLV_CHECK_EQ(p2.sequence, std::uint32_t{2});
}

OLV_TEST(olv2_batcher_second_batch_disjoint_from_first) {
  Olv2Batcher b(2);
  Frame f = makeFrame();
  f.objects.push_back(makeObject(1, true));

  b.push(f, 1.0);
  b.push(f, 2.0);
  std::uint32_t seq = 0;
  olv2::TrackPacket first = decodeOne(b.flush(seq)[0]);
  const double first_last_t = first.points.back().t;

  b.push(f, 3.0);
  b.push(f, 4.0);
  olv2::TrackPacket second = decodeOne(b.flush(seq)[0]);
  OLV_CHECK(second.points.front().t > first_last_t);
}

OLV_TEST(olv2_batcher_25_point_datagram_is_2128_bytes) {
  Olv2Batcher b(25);
  Frame f = makeFrame();
  f.objects.push_back(makeObject(1, true));
  for (int i = 0; i < 25; ++i) {
    b.push(f, 1.0 + static_cast<double>(i));
  }
  OLV_CHECK(b.ready());

  std::uint32_t seq = 0;
  const auto datagrams = b.flush(seq);
  OLV_CHECK_EQ(datagrams.size(), std::size_t{1});
  OLV_CHECK_EQ(datagrams[0].size(), std::size_t{2128});
  olv2::TrackPacket pkt = decodeOne(datagrams[0]);
  OLV_CHECK_EQ(pkt.points.size(), std::size_t{25});
}

OLV_TEST(olv2_batcher_every_datagram_decodes_ok) {
  Olv2Batcher b(4);
  for (int i = 0; i < 4; ++i) {
    Frame f = makeFrame();
    f.objects.push_back(makeObject(1, true));
    f.objects.push_back(makeObject(2, i % 2 == 0));
    b.push(f, 1.0 + static_cast<double>(i));
  }
  OLV_CHECK(b.ready());

  std::uint32_t seq = 0;
  const auto datagrams = b.flush(seq);
  OLV_CHECK_EQ(datagrams.size(), std::size_t{2});
  for (const auto& dg : datagrams) {
    olv2::TrackPacket pkt;
    OLV_CHECK(olv2::decode(dg.data(), dg.size(), pkt) == olv2::DecodeError::kNone);
  }
}
