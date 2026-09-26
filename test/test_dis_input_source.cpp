// test_dis_input_source.cpp — DisInputSource translation of DIS Entity State
// PDUs into StateStore (docs/FEATURE_INPUT_SOURCES.md §3/§5). Drives the full
// path via DisInputSource::processDatagram (no live socket) with hand-built
// Entity State PDU byte arrays, and inspects the resulting StateStore snapshot
// and stats.

#include <bit>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "olv/dis_input_source.hpp"
#include "olv/logger.hpp"
#include "olv/protocol.hpp"
#include "olv/state_store.hpp"

using namespace olv;
using namespace std::chrono;

namespace {

// --- Big-endian writers into a byte buffer (DIS is big-endian on the wire). --

void putBEU16(std::vector<std::uint8_t>& b, std::size_t off, std::uint16_t v) {
  b[off] = static_cast<std::uint8_t>(v >> 8);
  b[off + 1] = static_cast<std::uint8_t>(v & 0xFF);
}
void putBEU32(std::vector<std::uint8_t>& b, std::size_t off, std::uint32_t v) {
  b[off] = static_cast<std::uint8_t>(v >> 24);
  b[off + 1] = static_cast<std::uint8_t>(v >> 16);
  b[off + 2] = static_cast<std::uint8_t>(v >> 8);
  b[off + 3] = static_cast<std::uint8_t>(v);
}
void putBEF32(std::vector<std::uint8_t>& b, std::size_t off, float f) {
  putBEU32(b, off, std::bit_cast<std::uint32_t>(f));
}
void putBEF64(std::vector<std::uint8_t>& b, std::size_t off, double d) {
  const std::uint64_t v = std::bit_cast<std::uint64_t>(d);
  for (int i = 0; i < 8; ++i)
    b[off + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (56 - 8 * i));
}

// Minimal DIS6 Entity State PDU builder. The base PDU is 144 bytes; field
// offsets follow the open-dis unmarshal order (verified against the vendored
// library): header(12) entityID(6) forceId(1) numArtic(1) entityType(8)
// altEntityType(8) velocity(12 @36) location(24 @48) ... We only populate the
// fields DisInputSource consumes; the rest stay zero.
struct EsPdu {
  std::uint8_t version = 6;
  std::uint8_t exercise = 0;
  std::uint8_t pdu_type = 1;  // Entity State
  std::uint32_t timestamp = 0;
  std::uint16_t site = 1, app = 1, entity = 1;
  std::uint8_t kind = 1, domain = 5;  // (1,5) -> satellite type
  double px = 0.0, py = 0.0, pz = 0.0;
  float vx = 0.0f, vy = 0.0f, vz = 0.0f;

  std::vector<std::uint8_t> build() const {
    std::vector<std::uint8_t> b(144, 0);
    b[0] = version;
    b[1] = exercise;
    b[2] = pdu_type;
    // b[3] protocolFamily left 0
    putBEU32(b, 4, timestamp);
    putBEU16(b, 8, 144);  // length
    putBEU16(b, 12, site);
    putBEU16(b, 14, app);
    putBEU16(b, 16, entity);
    b[20] = kind;
    b[21] = domain;
    putBEF32(b, 36, vx);
    putBEF32(b, 40, vy);
    putBEF32(b, 44, vz);
    putBEF64(b, 48, px);
    putBEF64(b, 56, py);
    putBEF64(b, 64, pz);
    return b;
  }
};

DisInputConfig cfgFor(const std::string& sat = "1:1:1") {
  DisInputConfig c;
  c.bind_address = "127.0.0.1";
  c.port = 0;  // ephemeral: tests never receive, they call processDatagram
  c.satellite_entity_id = sat;
  return c;
}

void feed(DisInputSource& src, const EsPdu& p) {
  const std::vector<std::uint8_t> bytes = p.build();
  src.processDatagram(bytes.data(), bytes.size(), "127.0.0.1:9999");
}

Snapshot snap(StateStore& store) {
  return store.snapshot(steady_clock::now());
}

std::uint32_t fnv1a(std::uint16_t site, std::uint16_t app, std::uint16_t entity) {
  const std::uint8_t bytes[6] = {
      static_cast<std::uint8_t>(site >> 8),   static_cast<std::uint8_t>(site & 0xFF),
      static_cast<std::uint8_t>(app >> 8),    static_cast<std::uint8_t>(app & 0xFF),
      static_cast<std::uint8_t>(entity >> 8), static_cast<std::uint8_t>(entity & 0xFF)};
  std::uint32_t h = 2166136261u;
  for (std::uint8_t x : bytes) {
    h ^= x;
    h *= 16777619u;
  }
  return h;
}

}  // namespace

// ---------------------------------------------------------------------------
// parseDisEntityId (shared format parser).
// ---------------------------------------------------------------------------

TEST(DisInputSource, dis_parse_entity_id_valid) {
  std::uint16_t s = 0, a = 0, e = 0;
  EXPECT_TRUE(parseDisEntityId("12:34:56", s, a, e));
  EXPECT_EQ(s, 12);
  EXPECT_EQ(a, 34);
  EXPECT_EQ(e, 56);
  EXPECT_TRUE(parseDisEntityId("0:0:0", s, a, e));
  EXPECT_TRUE(parseDisEntityId("65535:65535:65535", s, a, e));
}

TEST(DisInputSource, dis_parse_entity_id_rejects_bad_format) {
  std::uint16_t s = 0, a = 0, e = 0;
  EXPECT_FALSE(parseDisEntityId("1:2", s, a, e));        // too few fields
  EXPECT_FALSE(parseDisEntityId("1:2:3:4", s, a, e));    // too many fields
  EXPECT_FALSE(parseDisEntityId("1::3", s, a, e));       // empty field
  EXPECT_FALSE(parseDisEntityId("1:2:", s, a, e));       // trailing empty field
  EXPECT_FALSE(parseDisEntityId("a:2:3", s, a, e));      // non-decimal
  EXPECT_FALSE(parseDisEntityId("1:2:65536", s, a, e));  // out of uint16 range
  EXPECT_FALSE(parseDisEntityId("", s, a, e));           // empty
  EXPECT_FALSE(parseDisEntityId(" 1:2:3", s, a, e));     // leading space
}

TEST(DisInputSource, dis_ctor_rejects_bad_satellite_id) {
  StateStore store;
  Logger log;
  EXPECT_THROW(DisInputSource(cfgFor("not-an-id"), store, log), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Satellite vs. object mapping (§3.3, §3.4).
// ---------------------------------------------------------------------------

TEST(DisInputSource, dis_satellite_entity_recognized) {
  StateStore store;
  Logger log;
  DisInputSource src(cfgFor("1:1:1"), store, log);

  EsPdu p;  // entity 1:1:1 == configured satellite
  p.px = 7000000.0;
  p.py = 100.0;
  p.pz = -200.0;
  p.vx = 1.5f;
  feed(src, p);

  Snapshot s = snap(store);
  EXPECT_TRUE(s.satellite.has_value());
  EXPECT_EQ(s.satellite->id, fnv1a(1, 1, 1));
  EXPECT_NEAR(s.satellite->px, 7000000.0, 1e-6);
  EXPECT_NEAR(s.satellite->pz, -200.0, 1e-6);
  EXPECT_NEAR(s.satellite->vx, 1.5, 1e-6);
  EXPECT_EQ(s.objects.size(), std::size_t{0});  // satellite is not an object
  EXPECT_EQ(s.stats.udp_accepted, std::uint64_t{1});
}

TEST(DisInputSource, dis_non_satellite_becomes_object) {
  StateStore store;
  Logger log;
  DisInputSource src(cfgFor("1:1:1"), store, log);

  EsPdu p;
  p.site = 2;
  p.app = 2;
  p.entity = 7;  // not the satellite
  p.kind = 1;
  p.domain = 1;  // (1,1) -> ground_hot
  p.px = 123.0;
  p.vz = -9.0f;
  feed(src, p);

  Snapshot s = snap(store);
  EXPECT_EQ(s.objects.size(), std::size_t{1});
  const SnapshotObject& o = s.objects.front();
  EXPECT_EQ(o.id, fnv1a(2, 2, 7));
  EXPECT_EQ(o.type, static_cast<std::uint8_t>(proto::ObjectType::kGroundHot));
  EXPECT_EQ(o.confidence, 100);
  EXPECT_TRUE(o.hasVelocity());
  EXPECT_NEAR(o.intensity, 0.0, 1e-9);
  EXPECT_NEAR(o.px, 123.0, 1e-6);
  EXPECT_NEAR(o.vz, -9.0, 1e-6);
  EXPECT_EQ(s.stats.udp_accepted, std::uint64_t{1});
}

TEST(DisInputSource, dis_entity_type_mapping_table) {
  struct Row {
    std::uint8_t kind, domain;
    proto::ObjectType want;
  };
  const Row rows[] = {
      {1, 5, proto::ObjectType::kSatellite}, {1, 1, proto::ObjectType::kGroundHot},
      {3, 1, proto::ObjectType::kGroundHot}, {2, 0, proto::ObjectType::kComet},
      {2, 9, proto::ObjectType::kComet},     {0, 5, proto::ObjectType::kDebris},
      {9, 9, proto::ObjectType::kUnknown},  // fallback
      {1, 2, proto::ObjectType::kUnknown},   {5, 5, proto::ObjectType::kUnknown},
      {0, 0, proto::ObjectType::kUnknown},
  };
  std::uint16_t ent = 10;
  for (const Row& r : rows) {
    StateStore store;
    Logger log;
    DisInputSource src(cfgFor("1:1:1"), store, log);
    EsPdu p;
    p.site = 2;
    p.app = 2;
    p.entity = ent++;  // distinct non-satellite entity each row
    p.kind = r.kind;
    p.domain = r.domain;
    feed(src, p);
    Snapshot s = snap(store);
    EXPECT_EQ(s.objects.size(), std::size_t{1});
    EXPECT_EQ(s.objects.front().type, static_cast<std::uint8_t>(r.want));
  }
}

TEST(DisInputSource, dis_space_platform_that_is_not_satellite_is_an_object) {
  // A (1,5) space platform that is not the configured satellite renders as a
  // kSatellite-typed *object*, not the satellite (§3.4 note).
  StateStore store;
  Logger log;
  DisInputSource src(cfgFor("1:1:1"), store, log);
  EsPdu p;
  p.site = 9;
  p.app = 9;
  p.entity = 9;
  p.kind = 1;
  p.domain = 5;
  feed(src, p);
  Snapshot s = snap(store);
  EXPECT_EQ(s.objects.size(), std::size_t{1});
  EXPECT_EQ(s.objects.front().type, static_cast<std::uint8_t>(proto::ObjectType::kSatellite));
}

TEST(DisInputSource, dis_distinct_entities_become_distinct_objects) {
  StateStore store;
  Logger log;
  DisInputSource src(cfgFor("1:1:1"), store, log);
  EsPdu a;
  a.site = 2;
  a.app = 2;
  a.entity = 100;
  EsPdu b;
  b.site = 2;
  b.app = 2;
  b.entity = 200;
  feed(src, a);
  feed(src, b);
  Snapshot s = snap(store);
  EXPECT_EQ(s.objects.size(), std::size_t{2});
  EXPECT_EQ(s.stats.udp_accepted, std::uint64_t{2});
}

TEST(DisInputSource, dis_satellite_carried_forward_across_object_pdus) {
  StateStore store;
  Logger log;
  DisInputSource src(cfgFor("1:1:1"), store, log);

  EsPdu sat;  // 1:1:1
  sat.px = 42.0;
  sat.py = 43.0;
  feed(src, sat);

  EsPdu obj;
  obj.site = 2;
  obj.app = 2;
  obj.entity = 5;
  feed(src, obj);

  Snapshot s = snap(store);
  EXPECT_TRUE(s.satellite.has_value());
  EXPECT_EQ(s.satellite->id, fnv1a(1, 1, 1));
  EXPECT_NEAR(s.satellite->px, 42.0, 1e-6);  // still the satellite's, not zeroed
  EXPECT_NEAR(s.satellite->py, 43.0, 1e-6);
  EXPECT_EQ(s.objects.size(), std::size_t{1});
}

// ---------------------------------------------------------------------------
// Filtering & unsupported kinds (§3.2, §3.5): counted in received only.
// ---------------------------------------------------------------------------

TEST(DisInputSource, dis_unsupported_pdu_kind_ignored_not_errored) {
  StateStore store;
  Logger log;
  DisInputSource src(cfgFor("1:1:1"), store, log);
  EsPdu p;
  p.pdu_type = 2;  // Fire PDU — recognized but unsupported
  feed(src, p);
  Snapshot s = snap(store);
  EXPECT_EQ(s.stats.udp_received, std::uint64_t{1});
  EXPECT_EQ(s.stats.udp_accepted, std::uint64_t{0});
  EXPECT_EQ(s.stats.udp_dropped_malformed, std::uint64_t{0});
  EXPECT_EQ(s.stats.udp_dropped_stale, std::uint64_t{0});
  EXPECT_FALSE(s.satellite.has_value());
  EXPECT_EQ(s.objects.size(), std::size_t{0});
}

TEST(DisInputSource, dis_exercise_filter_drops_mismatch_accepts_match) {
  StateStore store;
  Logger log;
  DisInputConfig c = cfgFor("1:1:1");
  c.exercise_id = 7;
  DisInputSource src(c, store, log);

  EsPdu other;  // 1:1:1 satellite but wrong exercise
  other.exercise = 3;
  feed(src, other);

  EsPdu match;
  match.exercise = 7;
  feed(src, match);

  Snapshot s = snap(store);
  EXPECT_EQ(s.stats.udp_received, std::uint64_t{2});
  EXPECT_EQ(s.stats.udp_accepted, std::uint64_t{1});           // only the matching one
  EXPECT_EQ(s.stats.udp_dropped_malformed, std::uint64_t{0});  // filtered != malformed
  EXPECT_EQ(s.stats.udp_dropped_stale, std::uint64_t{0});
}

TEST(DisInputSource, dis_no_exercise_filter_accepts_any_exercise) {
  StateStore store;
  Logger log;
  DisInputSource src(cfgFor("1:1:1"), store, log);  // exercise_id unset
  EsPdu p;
  p.exercise = 200;
  feed(src, p);
  EXPECT_EQ(snap(store).stats.udp_accepted, std::uint64_t{1});
}

// ---------------------------------------------------------------------------
// Malformed rejection (§3.2): counted as udp_dropped_malformed.
// ---------------------------------------------------------------------------

TEST(DisInputSource, dis_too_short_for_header_is_malformed) {
  StateStore store;
  Logger log;
  DisInputSource src(cfgFor("1:1:1"), store, log);
  const std::uint8_t buf[8] = {6, 0, 1, 0, 0, 0, 0, 0};  // < 12-byte header
  src.processDatagram(buf, sizeof(buf), "x");
  Snapshot s = snap(store);
  EXPECT_EQ(s.stats.udp_received, std::uint64_t{1});
  EXPECT_EQ(s.stats.udp_dropped_malformed, std::uint64_t{1});
  EXPECT_EQ(s.stats.udp_accepted, std::uint64_t{0});
}

TEST(DisInputSource, dis_truncated_body_is_malformed) {
  StateStore store;
  Logger log;
  DisInputSource src(cfgFor("1:1:1"), store, log);
  const std::vector<std::uint8_t> full = EsPdu{}.build();
  src.processDatagram(full.data(), 50, "x");  // header ok, body truncated
  Snapshot s = snap(store);
  EXPECT_EQ(s.stats.udp_dropped_malformed, std::uint64_t{1});
  EXPECT_EQ(s.stats.udp_accepted, std::uint64_t{0});
}

TEST(DisInputSource, dis_bad_protocol_version_is_malformed) {
  StateStore store;
  Logger log;
  DisInputSource src(cfgFor("1:1:1"), store, log);
  {
    EsPdu p;
    p.version = 4;  // below accepted 5-7
    feed(src, p);
  }
  {
    EsPdu p;
    p.version = 8;  // above accepted 5-7
    feed(src, p);
  }
  Snapshot s = snap(store);
  EXPECT_EQ(s.stats.udp_dropped_malformed, std::uint64_t{2});
  EXPECT_EQ(s.stats.udp_accepted, std::uint64_t{0});
}

TEST(DisInputSource, dis_protocol_versions_5_6_7_accepted) {
  for (std::uint8_t v : {std::uint8_t{5}, std::uint8_t{6}, std::uint8_t{7}}) {
    StateStore store;
    Logger log;
    DisInputSource src(cfgFor("1:1:1"), store, log);
    EsPdu p;
    p.version = v;
    feed(src, p);
    EXPECT_EQ(snap(store).stats.udp_accepted, std::uint64_t{1});
  }
}

// ---------------------------------------------------------------------------
// Per-entity timestamp staleness (§3.6).
// ---------------------------------------------------------------------------

TEST(DisInputSource, dis_older_timestamp_is_stale) {
  StateStore store;
  Logger log;
  DisInputSource src(cfgFor("1:1:1"), store, log);
  EsPdu p;
  p.site = 2;
  p.app = 2;
  p.entity = 3;

  p.timestamp = 1000u << 1;  // ts = 1000
  feed(src, p);
  p.timestamp = 800u << 1;  // ts = 800 (< 1000, small gap) -> stale
  feed(src, p);
  p.timestamp = 1200u << 1;  // ts = 1200 (>= last accepted 1000) -> accept
  feed(src, p);

  Snapshot s = snap(store);
  EXPECT_EQ(s.stats.udp_accepted, std::uint64_t{2});
  EXPECT_EQ(s.stats.udp_dropped_stale, std::uint64_t{1});
  EXPECT_EQ(s.stats.udp_dropped_malformed, std::uint64_t{0});
}

TEST(DisInputSource, dis_timestamp_zero_always_accepted) {
  StateStore store;
  Logger log;
  DisInputSource src(cfgFor("1:1:1"), store, log);
  EsPdu p;
  p.site = 2;
  p.app = 2;
  p.entity = 3;
  p.timestamp = 5000u << 1;  // ts = 5000
  feed(src, p);
  p.timestamp = 0;  // ts = 0 -> accepted despite being "older"
  feed(src, p);
  EXPECT_EQ(snap(store).stats.udp_accepted, std::uint64_t{2});
}

TEST(DisInputSource, dis_timestamp_wraparound_accepted) {
  StateStore store;
  Logger log;
  DisInputSource src(cfgFor("1:1:1"), store, log);
  EsPdu p;
  p.site = 2;
  p.app = 2;
  p.entity = 3;
  p.timestamp = 0x7FFFFFF0u << 1;  // ts = 0x7FFFFFF0 (near max), no shift loss
  // Note: (0x7FFFFFF0 << 1) fits in u32; ts = timestamp>>1 recovers 0x7FFFFFF0.
  feed(src, p);
  p.timestamp = 0x10u << 1;  // ts = 0x10, gap > 2^30 -> wraparound accept
  feed(src, p);
  EXPECT_EQ(snap(store).stats.udp_accepted, std::uint64_t{2});
  EXPECT_EQ(snap(store).stats.udp_dropped_stale, std::uint64_t{0});
}

TEST(DisInputSource, dis_staleness_is_per_entity) {
  StateStore store;
  Logger log;
  DisInputSource src(cfgFor("1:1:1"), store, log);
  EsPdu a;
  a.site = 2;
  a.app = 2;
  a.entity = 3;
  a.timestamp = 1000u << 1;
  feed(src, a);

  // A different entity with a smaller timestamp is not stale (separate track).
  EsPdu b;
  b.site = 2;
  b.app = 2;
  b.entity = 4;
  b.timestamp = 10u << 1;
  feed(src, b);

  Snapshot s = snap(store);
  EXPECT_EQ(s.stats.udp_accepted, std::uint64_t{2});
  EXPECT_EQ(s.stats.udp_dropped_stale, std::uint64_t{0});
}

TEST(DisInputSource, dis_synthesized_sequence_never_self_stales) {
  // Many accepted PDUs in a row must never trip apply()'s OLV1 sequence
  // staleness — DisInputSource synthesizes a strictly increasing sequence.
  StateStore store;
  Logger log;
  DisInputSource src(cfgFor("1:1:1"), store, log);
  for (std::uint16_t i = 0; i < 20; ++i) {
    EsPdu p;
    p.site = 2;
    p.app = 2;
    p.entity = static_cast<std::uint16_t>(500 + i);
    feed(src, p);
  }
  Snapshot s = snap(store);
  EXPECT_EQ(s.stats.udp_accepted, std::uint64_t{20});
  EXPECT_EQ(s.stats.udp_dropped_stale, std::uint64_t{0});
}
