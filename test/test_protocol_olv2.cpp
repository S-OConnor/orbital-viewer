// test_protocol_olv2.cpp — OLV2 encode/decode round-trip, exact byte layout,
// and every PROTOCOL_OLV2.md §4 validation item (including checked order),
// via the mutate+recompute-CRC pattern from test_validation.cpp.

#include <cstdint>
#include <limits>
#include <ostream>
#include <vector>

#include <gtest/gtest.h>

#include "olv/protocol_olv2.hpp"

using namespace olv;
namespace olv2 = olv::proto::olv2;

// Streamable for GoogleTest's failure messages (ADL in olv::proto::olv2).
namespace olv::proto::olv2 {
inline std::ostream& operator<<(std::ostream& os, DecodeError e) {
  return os << toString(e);
}
}  // namespace olv::proto::olv2

namespace {

olv2::Point makePoint(double t, double sat_px = 1000.0, double sat_py = 2000.0,
                      double sat_pz = 3000.0, double tgt_px = 5000.0, double tgt_py = 6000.0,
                      double tgt_pz = 7000.0,
                      std::uint8_t flags = olv2::kPointSatHasVel | olv2::kPointTgtHasVel,
                      float sat_vx = 1.0f, float sat_vy = 2.0f, float sat_vz = 3.0f,
                      float tgt_vx = 4.0f, float tgt_vy = 5.0f, float tgt_vz = 6.0f) {
  olv2::Point p;
  p.t = t;
  p.flags = flags;
  p.sat_px = sat_px;
  p.sat_py = sat_py;
  p.sat_pz = sat_pz;
  p.tgt_px = tgt_px;
  p.tgt_py = tgt_py;
  p.tgt_pz = tgt_pz;
  p.sat_vx = sat_vx;
  p.sat_vy = sat_vy;
  p.sat_vz = sat_vz;
  p.tgt_vx = tgt_vx;
  p.tgt_vy = tgt_vy;
  p.tgt_vz = tgt_vz;
  return p;
}

// A minimal valid packet with `n` strictly-ascending, finite, in-range points.
olv2::TrackPacket validPacket(std::size_t n) {
  olv2::TrackPacket pkt;
  pkt.sequence = 7;
  pkt.track_id = 2001;
  pkt.sat_id = 1;
  pkt.target_type = static_cast<std::uint8_t>(proto::ObjectType::kDebris);
  pkt.target_flags = proto::kFlagHighlight;
  pkt.confidence = 87;
  for (std::size_t i = 0; i < n; ++i) {
    pkt.points.push_back(makePoint(1000.0 + static_cast<double>(i)));
  }
  return pkt;
}

void checkPointEq(const olv2::Point& a, const olv2::Point& b) {
  EXPECT_EQ(a.t, b.t);
  EXPECT_EQ(a.flags, b.flags);
  EXPECT_EQ(a.sat_px, b.sat_px);
  EXPECT_EQ(a.sat_py, b.sat_py);
  EXPECT_EQ(a.sat_pz, b.sat_pz);
  EXPECT_EQ(a.tgt_px, b.tgt_px);
  EXPECT_EQ(a.tgt_py, b.tgt_py);
  EXPECT_EQ(a.tgt_pz, b.tgt_pz);
  EXPECT_EQ(a.sat_vx, b.sat_vx);
  EXPECT_EQ(a.sat_vy, b.sat_vy);
  EXPECT_EQ(a.sat_vz, b.sat_vz);
  EXPECT_EQ(a.tgt_vx, b.tgt_vx);
  EXPECT_EQ(a.tgt_vy, b.tgt_vy);
  EXPECT_EQ(a.tgt_vz, b.tgt_vz);
}

// Recompute the trailing CRC over all preceding bytes (mutate+refresh
// pattern, so a hand-tampered field can reach a later validation item).
void refreshCrc(std::vector<std::uint8_t>& b) {
  const std::uint32_t c = proto::crc32(b.data(), b.size() - olv2::kCrcSize);
  const std::size_t off = b.size() - olv2::kCrcSize;
  b[off] = static_cast<std::uint8_t>(c & 0xFF);
  b[off + 1] = static_cast<std::uint8_t>((c >> 8) & 0xFF);
  b[off + 2] = static_cast<std::uint8_t>((c >> 16) & 0xFF);
  b[off + 3] = static_cast<std::uint8_t>((c >> 24) & 0xFF);
}

olv2::DecodeError run(const std::vector<std::uint8_t>& b) {
  olv2::TrackPacket out;
  return olv2::decode(b.data(), b.size(), out);
}

}  // namespace

// --- Round-trip --------------------------------------------------------------

TEST(ProtocolOlv2, roundtrip_one_point) {
  olv2::TrackPacket pkt = validPacket(1);
  const auto b = olv2::encode(pkt);
  EXPECT_EQ(b.size(), olv2::kMinPacketSize);

  olv2::TrackPacket dec;
  ASSERT_EQ(olv2::decode(b.data(), b.size(), dec), olv2::DecodeError::kNone);
  EXPECT_EQ(dec.sequence, pkt.sequence);
  EXPECT_EQ(dec.track_id, pkt.track_id);
  EXPECT_EQ(dec.sat_id, pkt.sat_id);
  EXPECT_EQ(dec.target_type, pkt.target_type);
  EXPECT_EQ(dec.target_flags, pkt.target_flags);
  EXPECT_EQ(dec.confidence, pkt.confidence);
  ASSERT_EQ(dec.points.size(), std::size_t{1});
  checkPointEq(dec.points[0], pkt.points[0]);
}

TEST(ProtocolOlv2, roundtrip_twenty_five_points) {
  olv2::TrackPacket pkt = validPacket(25);
  const auto b = olv2::encode(pkt);
  EXPECT_EQ(b.size(), olv2::kMaxPacketSize);

  olv2::TrackPacket dec;
  ASSERT_EQ(olv2::decode(b.data(), b.size(), dec), olv2::DecodeError::kNone);
  ASSERT_EQ(dec.points.size(), std::size_t{25});
  for (std::size_t i = 0; i < 25; ++i) checkPointEq(dec.points[i], pkt.points[i]);
}

// --- Exact byte layout (docs/PROTOCOL_OLV2.md §3) ---------------------------

TEST(ProtocolOlv2, byte_layout_header_and_point_offsets) {
  olv2::TrackPacket pkt = validPacket(1);
  pkt.sequence = 0x11223344;
  pkt.track_id = 0x0A0B0C0D;
  pkt.sat_id = 0x01020304;
  pkt.target_type = static_cast<std::uint8_t>(proto::ObjectType::kSatellite);
  pkt.target_flags = proto::kFlagHighlight;
  pkt.confidence = 42;
  pkt.points[0] = makePoint(/*t=*/12345.5, /*sat_px=*/1.0, /*sat_py=*/2.0, /*sat_pz=*/3.0,
                            /*tgt_px=*/4.0, /*tgt_py=*/5.0, /*tgt_pz=*/6.0,
                            olv2::kPointSatHasVel | olv2::kPointTgtHasVel,
                            /*sat_vx=*/7.0f, /*sat_vy=*/8.0f, /*sat_vz=*/9.0f,
                            /*tgt_vx=*/10.0f, /*tgt_vy=*/11.0f, /*tgt_vz=*/12.0f);
  const auto b = olv2::encode(pkt);
  ASSERT_EQ(b.size(), olv2::kMinPacketSize);

  // Header, offsets 0..23, little-endian.
  EXPECT_EQ(b[0], 'O');
  EXPECT_EQ(b[1], 'L');
  EXPECT_EQ(b[2], 'V');
  EXPECT_EQ(b[3], '2');
  EXPECT_EQ(b[4], 1);  // version
  EXPECT_EQ(b[5], 1);  // msg_type = TRACK_UPDATE
  EXPECT_EQ(b[6], 0);  // hdr_flags reserved
  EXPECT_EQ(b[7], 0);
  EXPECT_EQ(proto::detail::getU32(b.data() + 8), 0x11223344u);                 // sequence
  EXPECT_EQ(proto::detail::getU32(b.data() + 12), 0x0A0B0C0Du);                // track_id
  EXPECT_EQ(proto::detail::getU32(b.data() + 16), 0x01020304u);                // sat_id
  EXPECT_EQ(b[20], static_cast<std::uint8_t>(proto::ObjectType::kSatellite));  // target_type
  EXPECT_EQ(b[21], proto::kFlagHighlight);                                     // target_flags
  EXPECT_EQ(b[22], 42);                                                        // confidence
  EXPECT_EQ(b[23], 1);                                                         // point_count

  // Point 0, offset 24, field offsets per docs/PROTOCOL_OLV2.md §3.
  const std::uint8_t* q = b.data() + 24;
  EXPECT_EQ(proto::detail::getF64(q + 0), 12345.5);                // t
  EXPECT_EQ(q[8], olv2::kPointSatHasVel | olv2::kPointTgtHasVel);  // point_flags
  EXPECT_EQ(q[9], 0);                                              // reserved
  EXPECT_EQ(q[10], 0);
  EXPECT_EQ(q[11], 0);
  EXPECT_EQ(proto::detail::getF64(q + 12), 1.0);    // sat_pos_x
  EXPECT_EQ(proto::detail::getF64(q + 20), 2.0);    // sat_pos_y
  EXPECT_EQ(proto::detail::getF64(q + 28), 3.0);    // sat_pos_z
  EXPECT_EQ(proto::detail::getF64(q + 36), 4.0);    // tgt_pos_x
  EXPECT_EQ(proto::detail::getF64(q + 44), 5.0);    // tgt_pos_y
  EXPECT_EQ(proto::detail::getF64(q + 52), 6.0);    // tgt_pos_z
  EXPECT_EQ(proto::detail::getF32(q + 60), 7.0f);   // sat_vel_x
  EXPECT_EQ(proto::detail::getF32(q + 64), 8.0f);   // sat_vel_y
  EXPECT_EQ(proto::detail::getF32(q + 68), 9.0f);   // sat_vel_z
  EXPECT_EQ(proto::detail::getF32(q + 72), 10.0f);  // tgt_vel_x
  EXPECT_EQ(proto::detail::getF32(q + 76), 11.0f);  // tgt_vel_y
  EXPECT_EQ(proto::detail::getF32(q + 80), 12.0f);  // tgt_vel_z

  // CRC trailer at offset 24 + 84*1 = 108.
  EXPECT_EQ(proto::detail::getU32(b.data() + 108),
            proto::crc32(b.data(), b.size() - olv2::kCrcSize));
}

// --- Validation items 1-10 (docs/PROTOCOL_OLV2.md §4) -----------------------

TEST(ProtocolOlv2, decode_too_short) {
  std::vector<std::uint8_t> b(olv2::kMinPacketSize - 1, 0);
  EXPECT_EQ(run(b), olv2::DecodeError::kTooShort);
}

TEST(ProtocolOlv2, decode_too_long) {
  std::vector<std::uint8_t> b(olv2::kMaxPacketSize + 1, 0);
  EXPECT_EQ(run(b), olv2::DecodeError::kTooLong);
}

TEST(ProtocolOlv2, decode_bad_magic) {
  auto b = olv2::encode(validPacket(1));
  b[0] = 'X';
  EXPECT_EQ(run(b), olv2::DecodeError::kBadMagic);
}

TEST(ProtocolOlv2, decode_bad_version) {
  auto b = olv2::encode(validPacket(1));
  b[4] = 2;
  EXPECT_EQ(run(b), olv2::DecodeError::kBadVersion);
}

TEST(ProtocolOlv2, decode_bad_msg_type) {
  auto b = olv2::encode(validPacket(1));
  b[5] = 2;
  EXPECT_EQ(run(b), olv2::DecodeError::kBadMsgType);
}

TEST(ProtocolOlv2, decode_bad_point_count_zero) {
  auto b = olv2::encode(validPacket(1));
  b[23] = 0;
  EXPECT_EQ(run(b), olv2::DecodeError::kBadPointCount);
}

TEST(ProtocolOlv2, decode_bad_point_count_over_25) {
  auto b = olv2::encode(validPacket(1));
  b[23] = 26;
  EXPECT_EQ(run(b), olv2::DecodeError::kBadPointCount);
}

TEST(ProtocolOlv2, decode_bad_length) {
  auto b = olv2::encode(validPacket(1));  // point_count=1, so length must be 112
  b.push_back(0);                         // one extra byte
  EXPECT_EQ(run(b), olv2::DecodeError::kBadLength);
}

TEST(ProtocolOlv2, decode_bad_crc) {
  auto b = olv2::encode(validPacket(1));
  b[12] ^= 0xFF;  // corrupt a track_id byte, leave CRC stale
  EXPECT_EQ(run(b), olv2::DecodeError::kBadCrc);
}

TEST(ProtocolOlv2, decode_non_finite_time) {
  auto pkt = validPacket(1);
  pkt.points[0].t = std::numeric_limits<double>::quiet_NaN();
  auto b = olv2::encode(pkt);  // CRC computed over the NaN bytes
  EXPECT_EQ(run(b), olv2::DecodeError::kNonFinite);
}

TEST(ProtocolOlv2, decode_non_finite_position) {
  auto pkt = validPacket(1);
  pkt.points[0].tgt_px = std::numeric_limits<double>::infinity();
  auto b = olv2::encode(pkt);
  EXPECT_EQ(run(b), olv2::DecodeError::kNonFinite);
}

TEST(ProtocolOlv2, decode_non_finite_velocity) {
  auto pkt = validPacket(1);
  pkt.points[0].sat_vx = std::numeric_limits<float>::quiet_NaN();
  auto b = olv2::encode(pkt);
  EXPECT_EQ(run(b), olv2::DecodeError::kNonFinite);
}

TEST(ProtocolOlv2, decode_out_of_range_satellite_position) {
  auto pkt = validPacket(1);
  pkt.points[0].sat_px = 2e13;  // > kMaxCoordinateMeters
  auto b = olv2::encode(pkt);
  EXPECT_EQ(run(b), olv2::DecodeError::kOutOfRange);
}

TEST(ProtocolOlv2, decode_out_of_range_target_position) {
  auto pkt = validPacket(1);
  pkt.points[0].tgt_pz = -2e13;
  auto b = olv2::encode(pkt);
  EXPECT_EQ(run(b), olv2::DecodeError::kOutOfRange);
}

TEST(ProtocolOlv2, decode_bad_time_non_positive_zero) {
  auto pkt = validPacket(1);
  pkt.points[0].t = 0.0;
  auto b = olv2::encode(pkt);
  EXPECT_EQ(run(b), olv2::DecodeError::kBadTime);
}

TEST(ProtocolOlv2, decode_bad_time_negative) {
  auto pkt = validPacket(1);
  pkt.points[0].t = -5.0;
  auto b = olv2::encode(pkt);
  EXPECT_EQ(run(b), olv2::DecodeError::kBadTime);
}

TEST(ProtocolOlv2, decode_bad_time_equal_consecutive) {
  auto pkt = validPacket(2);
  pkt.points[1].t = pkt.points[0].t;  // not strictly ascending
  auto b = olv2::encode(pkt);
  EXPECT_EQ(run(b), olv2::DecodeError::kBadTime);
}

TEST(ProtocolOlv2, decode_bad_time_decreasing) {
  auto pkt = validPacket(2);
  pkt.points[1].t = pkt.points[0].t - 1.0;
  auto b = olv2::encode(pkt);
  EXPECT_EQ(run(b), olv2::DecodeError::kBadTime);
}

// --- Validation order (docs/PROTOCOL_OLV2.md §4) ----------------------------

TEST(ProtocolOlv2, decode_order_bad_magic_before_bad_crc) {
  // Magic corruption alone already breaks the CRC (which covers the magic
  // bytes); bad_magic (item 2) must still win over bad_crc (item 7).
  auto b = olv2::encode(validPacket(1));
  b[1] = 'Z';
  EXPECT_EQ(run(b), olv2::DecodeError::kBadMagic);
}

TEST(ProtocolOlv2, decode_order_point_count_before_length) {
  // point_count=30 is invalid on its own AND makes the actual (unchanged)
  // datagram length wrong for that count too; point_count (item 5) is
  // checked before length (item 6), so bad_point_count wins.
  auto b = olv2::encode(validPacket(2));
  b[23] = 30;
  EXPECT_EQ(run(b), olv2::DecodeError::kBadPointCount);
}

TEST(ProtocolOlv2, decode_order_non_finite_before_range_same_point) {
  // A single point that is both out-of-range (tgt_px) and non-finite
  // (sat_vx): non_finite (item 8) is checked before out_of_range (item 9)
  // for that point.
  auto pkt = validPacket(1);
  pkt.points[0].tgt_px = 2e13;
  pkt.points[0].sat_vx = std::numeric_limits<float>::quiet_NaN();
  auto b = olv2::encode(pkt);
  EXPECT_EQ(run(b), olv2::DecodeError::kNonFinite);
}

TEST(ProtocolOlv2, decode_order_earlier_point_checked_before_later_point) {
  // Point 0 is out_of_range; point 1 is non_finite. Items 8/9 are evaluated
  // point by point in order, so point 0's out_of_range is reported before
  // point 1 is ever examined.
  auto pkt = validPacket(2);
  pkt.points[0].tgt_py = 2e13;
  pkt.points[1].tgt_px = std::numeric_limits<double>::quiet_NaN();
  auto b = olv2::encode(pkt);
  EXPECT_EQ(run(b), olv2::DecodeError::kOutOfRange);
}

TEST(ProtocolOlv2, decode_order_bad_time_only_after_all_points_finite_and_in_range) {
  // Point 0/1 share a time (would be bad_time), but point 1 is also
  // non-finite. Item 10 is only evaluated after every point has passed
  // items 8/9, so non_finite wins.
  auto pkt = validPacket(2);
  pkt.points[1].t = pkt.points[0].t;
  pkt.points[1].sat_pz = std::numeric_limits<double>::infinity();
  auto b = olv2::encode(pkt);
  EXPECT_EQ(run(b), olv2::DecodeError::kNonFinite);
}

// --- encode: truncation, clamping, masking ----------------------------------

TEST(ProtocolOlv2, encode_truncates_above_25_points) {
  olv2::TrackPacket pkt = validPacket(30);
  const auto b = olv2::encode(pkt);
  EXPECT_EQ(b.size(), olv2::kMaxPacketSize);

  olv2::TrackPacket dec;
  ASSERT_EQ(olv2::decode(b.data(), b.size(), dec), olv2::DecodeError::kNone);
  EXPECT_EQ(dec.points.size(), std::size_t{25});
}

TEST(ProtocolOlv2, encode_clamps_confidence_above_100) {
  auto pkt = validPacket(1);
  pkt.confidence = 255;
  const auto b = olv2::encode(pkt);
  EXPECT_EQ(b[22], 100);

  olv2::TrackPacket dec;
  ASSERT_EQ(olv2::decode(b.data(), b.size(), dec), olv2::DecodeError::kNone);
  EXPECT_EQ(dec.confidence, std::uint8_t{100});
}

TEST(ProtocolOlv2, encode_masks_target_flags_to_highlight) {
  auto pkt = validPacket(1);
  pkt.target_flags = 0xFF;  // all bits set
  const auto b = olv2::encode(pkt);
  EXPECT_EQ(b[21], proto::kFlagHighlight);  // only HIGHLIGHT bit survives

  olv2::TrackPacket dec;
  ASSERT_EQ(olv2::decode(b.data(), b.size(), dec), olv2::DecodeError::kNone);
  EXPECT_EQ(dec.target_flags, proto::kFlagHighlight);
}

TEST(ProtocolOlv2, encode_masks_point_flags_to_bits_0_and_1) {
  auto pkt = validPacket(1);
  pkt.points[0].flags = 0xFF;  // all bits set
  const auto b = olv2::encode(pkt);
  const auto expected = static_cast<std::uint8_t>(olv2::kPointSatHasVel | olv2::kPointTgtHasVel);
  EXPECT_EQ(b[24 + 8], expected);

  olv2::TrackPacket dec;
  ASSERT_EQ(olv2::decode(b.data(), b.size(), dec), olv2::DecodeError::kNone);
  EXPECT_EQ(dec.points[0].flags, expected);
}

// --- decode: normalization ---------------------------------------------------

TEST(ProtocolOlv2, decode_unknown_target_type_normalizes_to_zero) {
  for (std::uint8_t t : {std::uint8_t{6}, std::uint8_t{100}, std::uint8_t{255}}) {
    auto pkt = validPacket(1);
    pkt.target_type = t;
    auto b = olv2::encode(pkt);
    olv2::TrackPacket dec;
    ASSERT_EQ(olv2::decode(b.data(), b.size(), dec), olv2::DecodeError::kNone);
    EXPECT_EQ(dec.target_type, static_cast<std::uint8_t>(proto::ObjectType::kUnknown));
  }
}

TEST(ProtocolOlv2, decode_clamps_confidence_over_100_on_wire) {
  // Bypass encode()'s own clamp by tampering the wire byte directly.
  auto b = olv2::encode(validPacket(1));
  b[22] = 200;
  refreshCrc(b);

  olv2::TrackPacket dec;
  ASSERT_EQ(olv2::decode(b.data(), b.size(), dec), olv2::DecodeError::kNone);
  EXPECT_EQ(dec.confidence, std::uint8_t{100});
}

// --- toString ----------------------------------------------------------------

TEST(ProtocolOlv2, to_string_values) {
  EXPECT_STREQ(olv2::toString(olv2::DecodeError::kNone), "none");
  EXPECT_STREQ(olv2::toString(olv2::DecodeError::kTooShort), "too_short");
  EXPECT_STREQ(olv2::toString(olv2::DecodeError::kTooLong), "too_long");
  EXPECT_STREQ(olv2::toString(olv2::DecodeError::kBadMagic), "bad_magic");
  EXPECT_STREQ(olv2::toString(olv2::DecodeError::kBadVersion), "bad_version");
  EXPECT_STREQ(olv2::toString(olv2::DecodeError::kBadMsgType), "bad_msg_type");
  EXPECT_STREQ(olv2::toString(olv2::DecodeError::kBadPointCount), "bad_point_count");
  EXPECT_STREQ(olv2::toString(olv2::DecodeError::kBadLength), "bad_length");
  EXPECT_STREQ(olv2::toString(olv2::DecodeError::kBadCrc), "bad_crc");
  EXPECT_STREQ(olv2::toString(olv2::DecodeError::kNonFinite), "non_finite");
  EXPECT_STREQ(olv2::toString(olv2::DecodeError::kOutOfRange), "out_of_range");
  EXPECT_STREQ(olv2::toString(olv2::DecodeError::kBadTime), "bad_time");
}
