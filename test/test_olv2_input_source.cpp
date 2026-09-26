// test_olv2_input_source.cpp — Olv2InputSource translation of OLV2
// TRACK_UPDATE datagrams into StateStore (docs/features/FEATURE_OLV2.md §4/§5,
// docs/PROTOCOL_OLV2.md §5). Drives the full path via
// Olv2InputSource::processDatagram (no live socket) with packets built via
// olv::proto::olv2::encode, and inspects the resulting StateStore snapshot
// and stats.

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "olv/logger.hpp"
#include "olv/olv2_input_source.hpp"
#include "olv/protocol.hpp"
#include "olv/protocol_olv2.hpp"
#include "olv/state_store.hpp"

using namespace olv;
using namespace std::chrono;

namespace {

Olv2InputConfig cfgFor() {
  Olv2InputConfig c;
  c.bind_address = "127.0.0.1";
  c.port = 0;  // ephemeral: tests never receive, they call processDatagram
  return c;
}

void feed(Olv2InputSource& src, const proto::olv2::TrackPacket& pkt,
          const std::string& from = "127.0.0.1:9999") {
  const std::vector<std::uint8_t> bytes = proto::olv2::encode(pkt);
  src.processDatagram(bytes.data(), bytes.size(), from);
}

void feedBytes(Olv2InputSource& src, const std::vector<std::uint8_t>& bytes,
               const std::string& from = "127.0.0.1:9999") {
  src.processDatagram(bytes.data(), bytes.size(), from);
}

Snapshot snap(StateStore& store) {
  return store.snapshot(steady_clock::now());
}

}  // namespace

// ---------------------------------------------------------------------------
// Accepted datagram: target/satellite/trail mapping (§4, A3).
// ---------------------------------------------------------------------------

TEST(Olv2InputSource, olv2_accepted_datagram_sets_object_and_satellite_fields) {
  StateStore store;
  Logger log;
  Olv2InputSource src(cfgFor(), store, log);

  proto::olv2::TrackPacket pkt;
  pkt.sequence = 7;
  pkt.track_id = 1001;
  pkt.sat_id = 55;
  pkt.target_type = static_cast<std::uint8_t>(proto::ObjectType::kDebris);
  pkt.confidence = 88;

  proto::olv2::Point p0;
  p0.t = 100.0;
  p0.sat_px = 1000.0;
  p0.tgt_px = 10.0;
  p0.tgt_py = 20.0;
  p0.tgt_pz = 30.0;

  proto::olv2::Point p1;
  p1.t = 101.5;
  p1.flags = proto::olv2::kPointTgtHasVel;
  p1.sat_px = 2000.0;
  p1.sat_py = 5.0;
  p1.sat_pz = -5.0;
  p1.tgt_px = 11.0;
  p1.tgt_py = 21.0;
  p1.tgt_pz = 31.0;
  p1.tgt_vx = 1.0f;
  p1.tgt_vy = 2.0f;
  p1.tgt_vz = 3.0f;

  pkt.points = {p0, p1};
  feed(src, pkt);

  Snapshot s = snap(store);
  ASSERT_EQ(s.objects.size(), std::size_t{1});
  const SnapshotObject& o = s.objects.front();
  EXPECT_EQ(o.id, 1001u);
  EXPECT_EQ(o.type, static_cast<std::uint8_t>(proto::ObjectType::kDebris));
  EXPECT_EQ(o.confidence, 88);
  EXPECT_TRUE(o.hasVelocity());  // newest point (p1) has TGT_HAS_VEL
  EXPECT_NEAR(o.px, 11.0, 1e-9);
  EXPECT_NEAR(o.py, 21.0, 1e-9);
  EXPECT_NEAR(o.pz, 31.0, 1e-9);
  EXPECT_NEAR(o.vx, 1.0, 1e-6);
  EXPECT_NEAR(o.vy, 2.0, 1e-6);
  EXPECT_NEAR(o.vz, 3.0, 1e-6);
  EXPECT_NEAR(o.intensity, 0.0, 1e-9);

  ASSERT_TRUE(s.satellite.has_value());
  EXPECT_EQ(s.satellite->id, 55u);
  EXPECT_EQ(s.satellite->seq, 7u);
  EXPECT_NEAR(s.satellite->px, 2000.0, 1e-9);
  EXPECT_NEAR(s.satellite->py, 5.0, 1e-9);
  EXPECT_NEAR(s.satellite->pz, -5.0, 1e-9);
  // Neither point set SAT_HAS_VEL: estimated from the last two sat positions.
  EXPECT_NEAR(s.satellite->vx, (2000.0 - 1000.0) / 1.5, 1e-3);
  EXPECT_NEAR(s.satellite->vy, (5.0 - 0.0) / 1.5, 1e-3);
  EXPECT_NEAR(s.satellite->vz, (-5.0 - 0.0) / 1.5, 1e-3);

  ASSERT_EQ(s.trail_points.size(), std::size_t{2});
  EXPECT_EQ(s.trail_points[0].id, 1001u);
  EXPECT_NEAR(s.trail_points[0].t, 100.0, 1e-9);
  EXPECT_NEAR(s.trail_points[0].px, 10.0, 1e-9);
  EXPECT_EQ(s.trail_points[1].id, 1001u);
  EXPECT_NEAR(s.trail_points[1].t, 101.5, 1e-9);
  EXPECT_NEAR(s.trail_points[1].px, 11.0, 1e-9);

  EXPECT_EQ(s.stats.udp_accepted, std::uint64_t{1});
  EXPECT_EQ(s.stats.udp_received, std::uint64_t{1});
}

TEST(Olv2InputSource, olv2_first_datagram_for_track_always_accepted) {
  StateStore store;
  Logger log;
  Olv2InputSource src(cfgFor(), store, log);
  proto::olv2::TrackPacket pkt;
  pkt.track_id = 9;
  pkt.sat_id = 1;
  proto::olv2::Point p;
  p.t = 0.001;
  pkt.points = {p};
  feed(src, pkt);
  const Snapshot s = snap(store);
  EXPECT_EQ(s.stats.udp_accepted, std::uint64_t{1});
  EXPECT_EQ(s.stats.udp_dropped_stale, std::uint64_t{0});
}

// ---------------------------------------------------------------------------
// Malformed rejection: counted, state untouched.
// ---------------------------------------------------------------------------

TEST(Olv2InputSource, olv2_malformed_datagram_counted_and_state_untouched) {
  StateStore store;
  Logger log;
  Olv2InputSource src(cfgFor(), store, log);

  proto::olv2::TrackPacket pkt;
  pkt.track_id = 1;
  pkt.sat_id = 1;
  proto::olv2::Point p;
  p.t = 100.0;
  pkt.points = {p};
  const std::vector<std::uint8_t> good = proto::olv2::encode(pkt);

  // Too short (< kMinPacketSize).
  feedBytes(src, std::vector<std::uint8_t>(good.begin(), good.begin() + 50));
  // Bad CRC: flip a byte inside the point data, length unchanged.
  std::vector<std::uint8_t> bad_crc = good;
  bad_crc[30] ^= 0xFF;
  feedBytes(src, bad_crc);

  const Snapshot s = snap(store);
  EXPECT_EQ(s.stats.udp_received, std::uint64_t{2});
  EXPECT_EQ(s.stats.udp_dropped_malformed, std::uint64_t{2});
  EXPECT_EQ(s.stats.udp_accepted, std::uint64_t{0});
  EXPECT_FALSE(s.satellite.has_value());
  EXPECT_EQ(s.objects.size(), std::size_t{0});
  EXPECT_EQ(s.trail_points.size(), std::size_t{0});
}

// ---------------------------------------------------------------------------
// Per-track staleness (§4 item 11 / PROTOCOL_OLV2.md §4 item 11).
// ---------------------------------------------------------------------------

TEST(Olv2InputSource, olv2_stale_second_datagram_dropped_whole) {
  StateStore store;
  Logger log;
  Olv2InputSource src(cfgFor(), store, log);

  proto::olv2::TrackPacket first;
  first.track_id = 5;
  first.sat_id = 1;
  proto::olv2::Point p0;
  p0.t = 100.0;
  p0.tgt_px = 1.0;
  proto::olv2::Point p1;
  p1.t = 101.0;
  p1.tgt_px = 2.0;
  first.points = {p0, p1};
  feed(src, first);

  proto::olv2::TrackPacket second;
  second.track_id = 5;
  second.sat_id = 1;
  proto::olv2::Point q0;
  q0.t = 101.0;  // <= last accepted t (101.0) -> stale
  q0.tgt_px = 99.0;
  second.points = {q0};
  feed(src, second);

  const Snapshot s = snap(store);
  EXPECT_EQ(s.stats.udp_accepted, std::uint64_t{1});
  EXPECT_EQ(s.stats.udp_dropped_stale, std::uint64_t{1});
  ASSERT_EQ(s.objects.size(), std::size_t{1});
  EXPECT_NEAR(s.objects.front().px, 2.0, 1e-9);      // unchanged by the dropped datagram
  EXPECT_EQ(s.trail_points.size(), std::size_t{2});  // only the first datagram's points
}

TEST(Olv2InputSource, olv2_staleness_is_per_track) {
  StateStore store;
  Logger log;
  Olv2InputSource src(cfgFor(), store, log);

  proto::olv2::TrackPacket a;
  a.track_id = 1;
  a.sat_id = 1;
  proto::olv2::Point pa;
  pa.t = 1000.0;
  a.points = {pa};
  feed(src, a);

  // A different track with an earlier t is not stale (separate track); its
  // first datagram is always accepted.
  proto::olv2::TrackPacket b;
  b.track_id = 2;
  b.sat_id = 1;
  proto::olv2::Point pb;
  pb.t = 10.0;
  b.points = {pb};
  feed(src, b);

  const Snapshot s = snap(store);
  EXPECT_EQ(s.stats.udp_accepted, std::uint64_t{2});
  EXPECT_EQ(s.stats.udp_dropped_stale, std::uint64_t{0});
}

// ---------------------------------------------------------------------------
// Target flags (§4 mapping table).
// ---------------------------------------------------------------------------

TEST(Olv2InputSource, olv2_target_has_velocity_only_when_newest_point_flagged) {
  {
    StateStore store;
    Logger log;
    Olv2InputSource src(cfgFor(), store, log);
    proto::olv2::TrackPacket pkt;
    pkt.track_id = 1;
    pkt.sat_id = 1;
    proto::olv2::Point p0;
    p0.t = 1.0;
    p0.flags = proto::olv2::kPointTgtHasVel;
    p0.tgt_vx = 5.0f;
    proto::olv2::Point p1;
    p1.t = 2.0;  // newest: no TGT_HAS_VEL
    pkt.points = {p0, p1};
    feed(src, pkt);
    const Snapshot s = snap(store);
    ASSERT_EQ(s.objects.size(), std::size_t{1});
    EXPECT_FALSE(s.objects.front().hasVelocity());
    EXPECT_NEAR(s.objects.front().vx, 0.0, 1e-9);
  }
  {
    StateStore store;
    Logger log;
    Olv2InputSource src(cfgFor(), store, log);
    proto::olv2::TrackPacket pkt;
    pkt.track_id = 1;
    pkt.sat_id = 1;
    proto::olv2::Point p0;
    p0.t = 1.0;
    proto::olv2::Point p1;
    p1.t = 2.0;  // newest: TGT_HAS_VEL set
    p1.flags = proto::olv2::kPointTgtHasVel;
    p1.tgt_vx = 7.0f;
    pkt.points = {p0, p1};
    feed(src, pkt);
    const Snapshot s = snap(store);
    ASSERT_EQ(s.objects.size(), std::size_t{1});
    EXPECT_TRUE(s.objects.front().hasVelocity());
    EXPECT_NEAR(s.objects.front().vx, 7.0, 1e-6);
  }
}

TEST(Olv2InputSource, olv2_highlight_flag_passthrough) {
  StateStore store;
  Logger log;
  Olv2InputSource src(cfgFor(), store, log);
  proto::olv2::TrackPacket pkt;
  pkt.track_id = 1;
  pkt.sat_id = 1;
  pkt.target_flags = proto::kFlagHighlight;
  proto::olv2::Point p;
  p.t = 1.0;
  pkt.points = {p};
  feed(src, pkt);
  const Snapshot s = snap(store);
  ASSERT_EQ(s.objects.size(), std::size_t{1});
  EXPECT_NE(s.objects.front().flags & proto::kFlagHighlight, 0);
}

// ---------------------------------------------------------------------------
// Satellite velocity (A3): wire value, finite difference, or zero.
// ---------------------------------------------------------------------------

TEST(Olv2InputSource, olv2_satellite_velocity_from_wire_when_flag_set) {
  StateStore store;
  Logger log;
  Olv2InputSource src(cfgFor(), store, log);
  proto::olv2::TrackPacket pkt;
  pkt.track_id = 1;
  pkt.sat_id = 1;
  proto::olv2::Point p;
  p.t = 1.0;
  p.flags = proto::olv2::kPointSatHasVel;
  p.sat_vx = 100.0f;
  p.sat_vy = -50.0f;
  p.sat_vz = 3.0f;
  pkt.points = {p};
  feed(src, pkt);
  const Snapshot s = snap(store);
  ASSERT_TRUE(s.satellite.has_value());
  EXPECT_NEAR(s.satellite->vx, 100.0, 1e-6);
  EXPECT_NEAR(s.satellite->vy, -50.0, 1e-6);
  EXPECT_NEAR(s.satellite->vz, 3.0, 1e-6);
}

TEST(Olv2InputSource, olv2_satellite_velocity_finite_difference_when_flag_unset) {
  StateStore store;
  Logger log;
  Olv2InputSource src(cfgFor(), store, log);
  proto::olv2::TrackPacket pkt;
  pkt.track_id = 1;
  pkt.sat_id = 1;
  proto::olv2::Point p0;
  p0.t = 10.0;
  p0.sat_px = 100.0;
  p0.sat_py = 200.0;
  p0.sat_pz = 300.0;
  proto::olv2::Point p1;
  p1.t = 12.0;
  p1.sat_px = 110.0;
  p1.sat_py = 190.0;
  p1.sat_pz = 330.0;
  pkt.points = {p0, p1};
  feed(src, pkt);
  const Snapshot s = snap(store);
  ASSERT_TRUE(s.satellite.has_value());
  EXPECT_NEAR(s.satellite->vx, 5.0, 1e-4);   // (110-100)/2
  EXPECT_NEAR(s.satellite->vy, -5.0, 1e-4);  // (190-200)/2
  EXPECT_NEAR(s.satellite->vz, 15.0, 1e-4);  // (330-300)/2
}

TEST(Olv2InputSource, olv2_satellite_velocity_zero_with_single_point) {
  StateStore store;
  Logger log;
  Olv2InputSource src(cfgFor(), store, log);
  proto::olv2::TrackPacket pkt;
  pkt.track_id = 1;
  pkt.sat_id = 1;
  proto::olv2::Point p;
  p.t = 1.0;
  pkt.points = {p};
  feed(src, pkt);
  const Snapshot s = snap(store);
  ASSERT_TRUE(s.satellite.has_value());
  EXPECT_NEAR(s.satellite->vx, 0.0, 1e-9);
  EXPECT_NEAR(s.satellite->vy, 0.0, 1e-9);
  EXPECT_NEAR(s.satellite->vz, 0.0, 1e-9);
}

// ---------------------------------------------------------------------------
// Satellite newest-wins across interleaved tracks; sat_id change (§4).
// ---------------------------------------------------------------------------

TEST(Olv2InputSource, olv2_satellite_newest_wins_across_tracks) {
  StateStore store;
  Logger log;
  Olv2InputSource src(cfgFor(), store, log);

  proto::olv2::TrackPacket t1;
  t1.track_id = 1;
  t1.sat_id = 42;
  proto::olv2::Point p1;
  p1.t = 200.0;
  p1.sat_px = 111.0;
  p1.tgt_px = 1.0;
  t1.points = {p1};
  feed(src, t1);

  // Older overall sample time, but the first datagram for track 2 so it is
  // still accepted; it must not move the satellite backwards.
  proto::olv2::TrackPacket t2;
  t2.track_id = 2;
  t2.sat_id = 42;
  proto::olv2::Point p2;
  p2.t = 100.0;
  p2.sat_px = 999.0;
  p2.tgt_px = 2.0;
  t2.points = {p2};
  feed(src, t2);

  const Snapshot s = snap(store);
  ASSERT_TRUE(s.satellite.has_value());
  EXPECT_NEAR(s.satellite->px, 111.0, 1e-9);  // track 1's newer sample wins
  ASSERT_EQ(s.objects.size(), std::size_t{2});
  EXPECT_EQ(s.stats.udp_accepted, std::uint64_t{2});

  bool found_track2_trail = false;
  for (const TrailPoint& tp : s.trail_points) {
    if (tp.id == 2 && tp.t == 100.0) found_track2_trail = true;
  }
  EXPECT_TRUE(found_track2_trail);  // track 2's object and trail still applied
}

TEST(Olv2InputSource, olv2_sat_id_change_is_still_applied) {
  StateStore store;
  Logger log;
  Olv2InputSource src(cfgFor(), store, log);

  proto::olv2::TrackPacket a;
  a.track_id = 1;
  a.sat_id = 1;
  proto::olv2::Point pa;
  pa.t = 1.0;
  a.points = {pa};
  feed(src, a);

  proto::olv2::TrackPacket b;
  b.track_id = 1;
  b.sat_id = 2;  // changed, and newer t
  proto::olv2::Point pb;
  pb.t = 2.0;
  b.points = {pb};
  feed(src, b);

  const Snapshot s = snap(store);
  ASSERT_TRUE(s.satellite.has_value());
  EXPECT_EQ(s.satellite->id, 2u);
  EXPECT_EQ(s.stats.udp_accepted, std::uint64_t{2});
}

// ---------------------------------------------------------------------------
// udp_received counts every datagram regardless of outcome.
// ---------------------------------------------------------------------------

TEST(Olv2InputSource, olv2_udp_received_counts_every_datagram) {
  StateStore store;
  Logger log;
  Olv2InputSource src(cfgFor(), store, log);

  proto::olv2::TrackPacket ok;
  ok.track_id = 1;
  ok.sat_id = 1;
  proto::olv2::Point p0;
  p0.t = 1.0;
  ok.points = {p0};
  feed(src, ok);

  const std::vector<std::uint8_t> good = proto::olv2::encode(ok);
  feedBytes(src, std::vector<std::uint8_t>(good.begin(), good.begin() + 20));  // malformed

  proto::olv2::TrackPacket stale;
  stale.track_id = 1;
  stale.sat_id = 1;
  proto::olv2::Point p1;
  p1.t = 1.0;  // <= previous accepted t for track 1 -> stale
  stale.points = {p1};
  feed(src, stale);

  const Snapshot s = snap(store);
  EXPECT_EQ(s.stats.udp_received, std::uint64_t{3});
  EXPECT_EQ(s.stats.udp_accepted, std::uint64_t{1});
  EXPECT_EQ(s.stats.udp_dropped_malformed, std::uint64_t{1});
  EXPECT_EQ(s.stats.udp_dropped_stale, std::uint64_t{1});
}
