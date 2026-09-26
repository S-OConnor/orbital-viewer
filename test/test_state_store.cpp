// test_state_store.cpp — apply/staleness/merge/expiry/rate/snapshot semantics
// with explicitly injected wall/mono time points.

#include <chrono>
#include <cstdint>
#include <ostream>
#include <vector>

#include <gtest/gtest.h>

#include "olv/protocol.hpp"
#include "olv/state_store.hpp"

using namespace olv;
using namespace std::chrono;

// Streamable for GoogleTest's failure messages (ADL in olv).
namespace olv {
inline std::ostream& operator<<(std::ostream& os, ApplyResult r) {
  return os << (r == ApplyResult::kApplied ? "applied" : "stale");
}
}  // namespace olv

namespace {

const system_clock::time_point kW0{};
const steady_clock::time_point kM0{};

proto::StatePacket makePacket(std::uint32_t seq, std::uint32_t sat_id,
                              const std::vector<std::uint32_t>& ids, double sat_px = 1000.0) {
  proto::StatePacket p;
  p.sequence = seq;
  p.sat_id = sat_id;
  p.sat_px = sat_px;
  p.object_total = static_cast<std::uint16_t>(ids.size());
  for (std::uint32_t id : ids) {
    proto::ObjectRecord r;
    r.id = id;
    r.type = static_cast<std::uint8_t>(proto::ObjectType::kDebris);
    r.flags = proto::kFlagHasVelocity;
    r.confidence = 50;
    r.px = static_cast<double>(id) * 10.0;
    p.objects.push_back(r);
  }
  return p;
}

// OLV2: a store-neutral TrackUpdate for one track, as Olv2InputSource would
// build it (FEATURE_OLV2.md §5.2).
TrackUpdate makeTrack(std::uint32_t track_id, std::uint32_t sat_id, double sat_t, double sat_px,
                      std::vector<TrailPoint> trail = {}) {
  TrackUpdate u;
  u.satellite.id = sat_id;
  u.satellite.seq = 1;
  u.satellite.px = sat_px;
  u.satellite_t = sat_t;
  u.target.id = track_id;
  u.target.type = static_cast<std::uint8_t>(proto::ObjectType::kDebris);
  u.target.flags = proto::kFlagHasVelocity;
  u.target.confidence = 50;
  u.target.px = static_cast<double>(track_id) * 10.0;
  u.trail = std::move(trail);
  return u;
}

}  // namespace

TEST(StateStore, store_first_apply_accepted) {
  StateStore store;
  EXPECT_EQ(store.apply(makePacket(10, 1, {100}), kW0, kM0), ApplyResult::kApplied);
  Snapshot snap = store.snapshot(kM0);
  EXPECT_TRUE(snap.satellite.has_value());
  EXPECT_EQ(snap.objects.size(), std::size_t{1});
  EXPECT_TRUE(snap.last_data_time.has_value());
  EXPECT_EQ(snap.stats.udp_accepted, std::uint64_t{1});
}

TEST(StateStore, store_duplicate_seq_is_stale) {
  StateStore store;
  EXPECT_EQ(store.apply(makePacket(10, 1, {100}), kW0, kM0), ApplyResult::kApplied);
  EXPECT_EQ(store.apply(makePacket(10, 1, {100}), kW0, kM0), ApplyResult::kStaleSequence);
  Snapshot snap = store.snapshot(kM0);
  EXPECT_EQ(snap.stats.udp_accepted, std::uint64_t{1});
  EXPECT_EQ(snap.stats.udp_dropped_stale, std::uint64_t{1});
}

TEST(StateStore, store_older_seq_dropped) {
  StateStore store;
  EXPECT_EQ(store.apply(makePacket(10, 1, {100}), kW0, kM0), ApplyResult::kApplied);
  EXPECT_EQ(store.apply(makePacket(5, 1, {100}), kW0, kM0), ApplyResult::kStaleSequence);
}

TEST(StateStore, store_sequence_wraparound) {
  StateStore store;
  EXPECT_EQ(store.apply(makePacket(0xFFFFFFFFu, 1, {100}), kW0, kM0), ApplyResult::kApplied);
  EXPECT_EQ(store.apply(makePacket(0, 1, {100}), kW0, kM0), ApplyResult::kApplied);
  // Going back to the old max is now stale.
  EXPECT_EQ(store.apply(makePacket(0xFFFFFFFFu, 1, {100}), kW0, kM0), ApplyResult::kStaleSequence);
}

TEST(StateStore, store_object_upsert_merges_by_id) {
  StateStore store;
  store.apply(makePacket(1, 1, {1, 2}), kW0, kM0);
  // Second packet updates id 2 (new px) and adds id 3.
  proto::StatePacket p2 = makePacket(2, 1, {2, 3});
  p2.objects[0].px = 99999.0;  // id 2 updated value
  store.apply(p2, kW0, kM0);

  Snapshot snap = store.snapshot(kM0);
  EXPECT_EQ(snap.objects.size(), std::size_t{3});
  EXPECT_EQ(snap.objects[0].id, std::uint32_t{1});
  EXPECT_EQ(snap.objects[1].id, std::uint32_t{2});
  EXPECT_EQ(snap.objects[2].id, std::uint32_t{3});
  EXPECT_EQ(snap.objects[1].px, 99999.0);
}

TEST(StateStore, store_expiry_prunes_after_15s) {
  StateStore store;  // default expiry 15 s
  store.apply(makePacket(1, 1, {100}), kW0, kM0);

  // Exactly at the window boundary the object is retained.
  Snapshot at_boundary = store.snapshot(kM0 + seconds{15});
  EXPECT_EQ(at_boundary.objects.size(), std::size_t{1});

  // One millisecond past the window it is pruned.
  Snapshot past = store.snapshot(kM0 + seconds{15} + milliseconds{1});
  EXPECT_EQ(past.objects.size(), std::size_t{0});
}

TEST(StateStore, store_satellite_replaced_wholesale) {
  StateStore store;
  store.apply(makePacket(1, 1, {100}, /*sat_px=*/1000.0), kW0, kM0);
  store.apply(makePacket(2, 2, {100}, /*sat_px=*/5000.0), kW0, kM0);
  Snapshot snap = store.snapshot(kM0);
  EXPECT_TRUE(snap.satellite.has_value());
  EXPECT_EQ(snap.satellite->id, std::uint32_t{2});
  EXPECT_EQ(snap.satellite->seq, std::uint32_t{2});
  EXPECT_EQ(snap.satellite->px, 5000.0);
}

TEST(StateStore, store_last_data_time_updates) {
  StateStore store;
  const auto w1 = kW0 + seconds{100};
  const auto w2 = kW0 + seconds{200};
  store.apply(makePacket(1, 1, {100}), w1, kM0);
  EXPECT_TRUE(store.snapshot(kM0).last_data_time == w1);
  store.apply(makePacket(2, 1, {100}), w2, kM0);
  EXPECT_TRUE(store.snapshot(kM0).last_data_time == w2);
}

TEST(StateStore, store_rate_window_five_in_five_seconds) {
  StateStore store;
  for (int i = 0; i < 5; ++i) {
    store.apply(makePacket(static_cast<std::uint32_t>(i + 1), 1, {100}), kW0, kM0 + seconds{i});
  }
  // Snapshot at t0+4s: all five accepts fall within the trailing 5 s window.
  Snapshot snap = store.snapshot(kM0 + seconds{4});
  EXPECT_NEAR(snap.stats.udp_rate_hz, 1.0, 1e-9);
}

TEST(StateStore, store_count_totals) {
  StateStore store;
  store.countReceived(100);
  store.countReceived(100);
  store.countReceived(100);
  store.countDropped(DropKind::kMalformed);
  store.countDropped(DropKind::kMalformed);
  store.countDropped(DropKind::kStale);
  Snapshot snap = store.snapshot(kM0);
  EXPECT_EQ(snap.stats.udp_received, std::uint64_t{3});
  EXPECT_EQ(snap.stats.bytes_received, std::uint64_t{300});
  EXPECT_EQ(snap.stats.udp_dropped_malformed, std::uint64_t{2});
  EXPECT_EQ(snap.stats.udp_dropped_stale, std::uint64_t{1});
}

TEST(StateStore, store_snapshot_sorted_by_id) {
  StateStore store;
  store.apply(makePacket(1, 1, {5, 1, 3, 2, 4}), kW0, kM0);
  Snapshot snap = store.snapshot(kM0);
  EXPECT_EQ(snap.objects.size(), std::size_t{5});
  for (std::size_t i = 0; i < snap.objects.size(); ++i) {
    EXPECT_EQ(snap.objects[i].id, static_cast<std::uint32_t>(i + 1));
  }
}

// --- applyTrack (OLV2, FEATURE_OLV2.md §5.2) --------------------------------

TEST(StateStore, store_apply_track_counts_and_last_data_time) {
  StateStore store;
  const auto w1 = kW0 + seconds{50};
  store.applyTrack(makeTrack(100, 1, /*sat_t=*/10.0, /*sat_px=*/1000.0), w1, kM0);
  Snapshot snap = store.snapshot(kM0);
  EXPECT_EQ(snap.stats.udp_accepted, std::uint64_t{1});
  ASSERT_TRUE(snap.last_data_time.has_value());
  EXPECT_TRUE(*snap.last_data_time == w1);
}

TEST(StateStore, store_apply_track_rate_window_five_in_five_seconds) {
  StateStore store;
  for (int i = 0; i < 5; ++i) {
    store.applyTrack(makeTrack(100, 1, 10.0 + i, 1000.0), kW0, kM0 + seconds{i});
  }
  Snapshot snap = store.snapshot(kM0 + seconds{4});
  EXPECT_NEAR(snap.stats.udp_rate_hz, 1.0, 1e-9);
}

TEST(StateStore, store_apply_track_upserts_target_and_expires_like_other_objects) {
  StateStore store;
  store.applyTrack(makeTrack(200, 1, 10.0, 1000.0), kW0, kM0);
  Snapshot snap = store.snapshot(kM0);
  ASSERT_EQ(snap.objects.size(), std::size_t{1});
  EXPECT_EQ(snap.objects[0].id, std::uint32_t{200});

  // Exactly at the window boundary the object is retained; one ms past, pruned.
  Snapshot at_boundary = store.snapshot(kM0 + seconds{15});
  EXPECT_EQ(at_boundary.objects.size(), std::size_t{1});
  Snapshot past = store.snapshot(kM0 + seconds{15} + milliseconds{1});
  EXPECT_EQ(past.objects.size(), std::size_t{0});
}

TEST(StateStore, store_apply_track_satellite_newest_wins_across_tracks) {
  StateStore store;
  store.applyTrack(makeTrack(1, 1, /*sat_t=*/100.0, /*sat_px=*/1000.0), kW0, kM0);

  // Older satellite_t: not replaced.
  store.applyTrack(makeTrack(2, 1, /*sat_t=*/50.0, /*sat_px=*/5000.0), kW0, kM0);
  EXPECT_EQ(store.snapshot(kM0).satellite->px, 1000.0);

  // Equal satellite_t: not replaced (strictly greater required).
  store.applyTrack(makeTrack(3, 1, /*sat_t=*/100.0, /*sat_px=*/6000.0), kW0, kM0);
  EXPECT_EQ(store.snapshot(kM0).satellite->px, 1000.0);

  // Newer satellite_t: replaced.
  store.applyTrack(makeTrack(4, 1, /*sat_t=*/200.0, /*sat_px=*/7000.0), kW0, kM0);
  EXPECT_EQ(store.snapshot(kM0).satellite->px, 7000.0);
}

TEST(StateStore, store_apply_track_replaces_satellite_that_came_from_apply) {
  StateStore store;
  store.apply(makePacket(1, 1, {100}, /*sat_px=*/1000.0), kW0, kM0);
  EXPECT_EQ(store.snapshot(kM0).satellite->px, 1000.0);

  // A stored satellite that came from apply() has no OLV2 sample time, so
  // applyTrack always replaces it, even with a very old satellite_t.
  store.applyTrack(makeTrack(2, 1, /*sat_t=*/1.0, /*sat_px=*/5000.0), kW0, kM0);
  EXPECT_EQ(store.snapshot(kM0).satellite->px, 5000.0);
}

TEST(StateStore, store_apply_track_does_not_affect_apply_olv1_sequence_staleness) {
  StateStore store;
  EXPECT_EQ(store.apply(makePacket(10, 1, {100}), kW0, kM0), ApplyResult::kApplied);
  store.applyTrack(makeTrack(200, 1, /*sat_t=*/5.0, /*sat_px=*/1000.0), kW0, kM0);

  // OLV1 sequence progression is untouched by the interleaved applyTrack call.
  EXPECT_EQ(store.apply(makePacket(11, 1, {100}), kW0, kM0), ApplyResult::kApplied);
  EXPECT_EQ(store.apply(makePacket(11, 1, {100}), kW0, kM0), ApplyResult::kStaleSequence);
}

TEST(StateStore, store_apply_then_apply_track_then_apply_resets_satellite_state) {
  StateStore store;
  store.apply(makePacket(1, 1, {100}, /*sat_px=*/1000.0), kW0, kM0);
  store.applyTrack(makeTrack(2, 1, /*sat_t=*/100.0, /*sat_px=*/5000.0), kW0, kM0);
  EXPECT_EQ(store.snapshot(kM0).satellite->px, 5000.0);

  // A later apply() replaces the satellite wholesale and clears the OLV2
  // sample-time state.
  store.apply(makePacket(2, 1, {100}, /*sat_px=*/9000.0), kW0, kM0);
  EXPECT_EQ(store.snapshot(kM0).satellite->px, 9000.0);

  // Because satellite_t_ was cleared by apply(), the next applyTrack replaces
  // unconditionally even with an "older" satellite_t.
  store.applyTrack(makeTrack(3, 1, /*sat_t=*/1.0, /*sat_px=*/7000.0), kW0, kM0);
  EXPECT_EQ(store.snapshot(kM0).satellite->px, 7000.0);
}

TEST(StateStore, store_apply_track_trail_points_drained_exactly_once) {
  StateStore store;
  std::vector<TrailPoint> trail = {TrailPoint{100, 1.0, 10.0, 20.0, 30.0},
                                   TrailPoint{100, 2.0, 11.0, 21.0, 31.0}};
  store.applyTrack(makeTrack(100, 1, /*sat_t=*/2.0, 1000.0, trail), kW0, kM0);

  Snapshot snap1 = store.snapshot(kM0);
  ASSERT_EQ(snap1.trail_points.size(), std::size_t{2});
  EXPECT_EQ(snap1.trail_points[0].t, 1.0);
  EXPECT_EQ(snap1.trail_points[1].t, 2.0);

  Snapshot snap2 = store.snapshot(kM0);
  EXPECT_TRUE(snap2.trail_points.empty());
}

TEST(StateStore, store_apply_track_trail_order_preserved_across_calls) {
  StateStore store;
  store.applyTrack(makeTrack(100, 1, 1.0, 1000.0, {TrailPoint{100, 1.0, 0.0, 0.0, 0.0}}), kW0, kM0);
  store.applyTrack(makeTrack(200, 1, 2.0, 1000.0, {TrailPoint{200, 2.0, 0.0, 0.0, 0.0}}), kW0, kM0);
  store.applyTrack(makeTrack(100, 1, 3.0, 1000.0, {TrailPoint{100, 3.0, 0.0, 0.0, 0.0}}), kW0, kM0);

  Snapshot snap = store.snapshot(kM0);
  ASSERT_EQ(snap.trail_points.size(), std::size_t{3});
  EXPECT_EQ(snap.trail_points[0].id, std::uint32_t{100});
  EXPECT_EQ(snap.trail_points[1].id, std::uint32_t{200});
  EXPECT_EQ(snap.trail_points[2].id, std::uint32_t{100});
  EXPECT_EQ(snap.trail_points[0].t, 1.0);
  EXPECT_EQ(snap.trail_points[1].t, 2.0);
  EXPECT_EQ(snap.trail_points[2].t, 3.0);
}

TEST(StateStore, store_apply_track_trail_cap_drops_oldest) {
  StateStore store;
  constexpr std::size_t kMax = StateStore::kMaxPendingTrailPoints;
  constexpr std::size_t kChunk = 25000;  // 5 * 25000 = kMax, plus one 10-point chunk
  std::size_t next_id = 0;
  std::uint32_t track = 1;

  auto pushChunk = [&](std::size_t n) {
    std::vector<TrailPoint> trail;
    trail.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
      TrailPoint tp;
      tp.id = static_cast<std::uint32_t>(next_id);
      tp.t = static_cast<double>(next_id);
      trail.push_back(tp);
      ++next_id;
    }
    store.applyTrack(makeTrack(track++, 1, static_cast<double>(next_id), 1000.0, trail), kW0, kM0);
  };

  for (int i = 0; i < 5; ++i) pushChunk(kChunk);
  pushChunk(10);  // total pushed: kMax + 10

  Snapshot snap = store.snapshot(kM0);
  EXPECT_EQ(snap.trail_points.size(), kMax);
  // The oldest 10 points (id/t 0..9) were dropped; the first survivor is 10.
  EXPECT_EQ(snap.trail_points.front().id, std::uint32_t{10});
  EXPECT_EQ(snap.trail_points.front().t, 10.0);
}
