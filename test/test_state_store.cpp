// test_state_store.cpp — apply/staleness/merge/expiry/rate/snapshot semantics
// with explicitly injected wall/mono time points.

#include <chrono>
#include <cstdint>
#include <ostream>
#include <vector>

#include "olv/protocol.hpp"
#include "olv_test.hpp"
#include "olv/state_store.hpp"

using namespace olv;
using namespace std::chrono;

// Streamable for the test framework's failure reporter (ADL in olv).
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

}  // namespace

OLV_TEST(store_first_apply_accepted) {
  StateStore store;
  OLV_CHECK_EQ(store.apply(makePacket(10, 1, {100}), kW0, kM0), ApplyResult::kApplied);
  Snapshot snap = store.snapshot(kM0);
  OLV_CHECK(snap.satellite.has_value());
  OLV_CHECK_EQ(snap.objects.size(), std::size_t{1});
  OLV_CHECK(snap.last_data_time.has_value());
  OLV_CHECK_EQ(snap.stats.udp_accepted, std::uint64_t{1});
}

OLV_TEST(store_duplicate_seq_is_stale) {
  StateStore store;
  OLV_CHECK_EQ(store.apply(makePacket(10, 1, {100}), kW0, kM0), ApplyResult::kApplied);
  OLV_CHECK_EQ(store.apply(makePacket(10, 1, {100}), kW0, kM0), ApplyResult::kStaleSequence);
  Snapshot snap = store.snapshot(kM0);
  OLV_CHECK_EQ(snap.stats.udp_accepted, std::uint64_t{1});
  OLV_CHECK_EQ(snap.stats.udp_dropped_stale, std::uint64_t{1});
}

OLV_TEST(store_older_seq_dropped) {
  StateStore store;
  OLV_CHECK_EQ(store.apply(makePacket(10, 1, {100}), kW0, kM0), ApplyResult::kApplied);
  OLV_CHECK_EQ(store.apply(makePacket(5, 1, {100}), kW0, kM0), ApplyResult::kStaleSequence);
}

OLV_TEST(store_sequence_wraparound) {
  StateStore store;
  OLV_CHECK_EQ(store.apply(makePacket(0xFFFFFFFFu, 1, {100}), kW0, kM0), ApplyResult::kApplied);
  OLV_CHECK_EQ(store.apply(makePacket(0, 1, {100}), kW0, kM0), ApplyResult::kApplied);
  // Going back to the old max is now stale.
  OLV_CHECK_EQ(store.apply(makePacket(0xFFFFFFFFu, 1, {100}), kW0, kM0),
               ApplyResult::kStaleSequence);
}

OLV_TEST(store_object_upsert_merges_by_id) {
  StateStore store;
  store.apply(makePacket(1, 1, {1, 2}), kW0, kM0);
  // Second packet updates id 2 (new px) and adds id 3.
  proto::StatePacket p2 = makePacket(2, 1, {2, 3});
  p2.objects[0].px = 99999.0;  // id 2 updated value
  store.apply(p2, kW0, kM0);

  Snapshot snap = store.snapshot(kM0);
  OLV_CHECK_EQ(snap.objects.size(), std::size_t{3});
  OLV_CHECK_EQ(snap.objects[0].id, std::uint32_t{1});
  OLV_CHECK_EQ(snap.objects[1].id, std::uint32_t{2});
  OLV_CHECK_EQ(snap.objects[2].id, std::uint32_t{3});
  OLV_CHECK_EQ(snap.objects[1].px, 99999.0);
}

OLV_TEST(store_expiry_prunes_after_15s) {
  StateStore store;  // default expiry 15 s
  store.apply(makePacket(1, 1, {100}), kW0, kM0);

  // Exactly at the window boundary the object is retained.
  Snapshot at_boundary = store.snapshot(kM0 + seconds{15});
  OLV_CHECK_EQ(at_boundary.objects.size(), std::size_t{1});

  // One millisecond past the window it is pruned.
  Snapshot past = store.snapshot(kM0 + seconds{15} + milliseconds{1});
  OLV_CHECK_EQ(past.objects.size(), std::size_t{0});
}

OLV_TEST(store_satellite_replaced_wholesale) {
  StateStore store;
  store.apply(makePacket(1, 1, {100}, /*sat_px=*/1000.0), kW0, kM0);
  store.apply(makePacket(2, 2, {100}, /*sat_px=*/5000.0), kW0, kM0);
  Snapshot snap = store.snapshot(kM0);
  OLV_CHECK(snap.satellite.has_value());
  OLV_CHECK_EQ(snap.satellite->id, std::uint32_t{2});
  OLV_CHECK_EQ(snap.satellite->seq, std::uint32_t{2});
  OLV_CHECK_EQ(snap.satellite->px, 5000.0);
}

OLV_TEST(store_last_data_time_updates) {
  StateStore store;
  const auto w1 = kW0 + seconds{100};
  const auto w2 = kW0 + seconds{200};
  store.apply(makePacket(1, 1, {100}), w1, kM0);
  OLV_CHECK(store.snapshot(kM0).last_data_time == w1);
  store.apply(makePacket(2, 1, {100}), w2, kM0);
  OLV_CHECK(store.snapshot(kM0).last_data_time == w2);
}

OLV_TEST(store_rate_window_five_in_five_seconds) {
  StateStore store;
  for (int i = 0; i < 5; ++i) {
    store.apply(makePacket(static_cast<std::uint32_t>(i + 1), 1, {100}), kW0, kM0 + seconds{i});
  }
  // Snapshot at t0+4s: all five accepts fall within the trailing 5 s window.
  Snapshot snap = store.snapshot(kM0 + seconds{4});
  OLV_CHECK_NEAR(snap.stats.udp_rate_hz, 1.0, 1e-9);
}

OLV_TEST(store_count_totals) {
  StateStore store;
  store.countReceived(100);
  store.countReceived(100);
  store.countReceived(100);
  store.countDropped(DropKind::kMalformed);
  store.countDropped(DropKind::kMalformed);
  store.countDropped(DropKind::kStale);
  Snapshot snap = store.snapshot(kM0);
  OLV_CHECK_EQ(snap.stats.udp_received, std::uint64_t{3});
  OLV_CHECK_EQ(snap.stats.bytes_received, std::uint64_t{300});
  OLV_CHECK_EQ(snap.stats.udp_dropped_malformed, std::uint64_t{2});
  OLV_CHECK_EQ(snap.stats.udp_dropped_stale, std::uint64_t{1});
}

OLV_TEST(store_snapshot_sorted_by_id) {
  StateStore store;
  store.apply(makePacket(1, 1, {5, 1, 3, 2, 4}), kW0, kM0);
  Snapshot snap = store.snapshot(kM0);
  OLV_CHECK_EQ(snap.objects.size(), std::size_t{5});
  for (std::size_t i = 0; i < snap.objects.size(); ++i) {
    OLV_CHECK_EQ(snap.objects[i].id, static_cast<std::uint32_t>(i + 1));
  }
}
