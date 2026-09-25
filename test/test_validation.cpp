// test_validation.cpp — every proto::decode DecodeError path via crafted bytes.

#include <cstdint>
#include <cstring>
#include <limits>
#include <ostream>
#include <vector>

#include "olv/protocol.hpp"
#include "olv_test.hpp"

using namespace olv;

// Streamable for the test framework's failure reporter (ADL in olv::proto).
namespace olv::proto {
inline std::ostream& operator<<(std::ostream& os, DecodeError e) {
  return os << toString(e);
}
}  // namespace olv::proto

namespace {

// A minimal valid packet with `n` debris objects (finite, in range).
proto::StatePacket validPacket(std::uint16_t n) {
  proto::StatePacket pkt;
  pkt.sequence = 1;
  pkt.sat_id = 1;
  pkt.sat_px = 1000.0;
  pkt.object_total = n;
  for (std::uint16_t i = 0; i < n; ++i) {
    proto::ObjectRecord r;
    r.id = 100 + i;
    r.type = static_cast<std::uint8_t>(proto::ObjectType::kDebris);
    r.flags = proto::kFlagHasVelocity;
    r.confidence = 50;
    r.px = 2000.0 + i;
    pkt.objects.push_back(r);
  }
  return pkt;
}

void setU16(std::vector<std::uint8_t>& b, std::size_t off, std::uint16_t v) {
  b[off] = static_cast<std::uint8_t>(v & 0xFF);
  b[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
}

// Recompute the trailing CRC over all preceding bytes.
void refreshCrc(std::vector<std::uint8_t>& b) {
  const std::uint32_t c = proto::crc32(b.data(), b.size() - proto::kCrcSize);
  const std::size_t off = b.size() - proto::kCrcSize;
  b[off] = static_cast<std::uint8_t>(c & 0xFF);
  b[off + 1] = static_cast<std::uint8_t>((c >> 8) & 0xFF);
  b[off + 2] = static_cast<std::uint8_t>((c >> 16) & 0xFF);
  b[off + 3] = static_cast<std::uint8_t>((c >> 24) & 0xFF);
}

proto::DecodeError run(const std::vector<std::uint8_t>& b) {
  proto::StatePacket out;
  return proto::decode(b.data(), b.size(), out);
}

}  // namespace

OLV_TEST(decode_too_short) {
  std::vector<std::uint8_t> b(59, 0);
  OLV_CHECK_EQ(run(b), proto::DecodeError::kTooShort);
}

OLV_TEST(decode_too_long) {
  std::vector<std::uint8_t> b(6205, 0);
  OLV_CHECK_EQ(run(b), proto::DecodeError::kTooLong);
}

OLV_TEST(decode_bad_magic) {
  auto b = proto::encode(validPacket(0));
  b[0] = 'X';
  OLV_CHECK_EQ(run(b), proto::DecodeError::kBadMagic);
}

OLV_TEST(decode_bad_version) {
  auto b = proto::encode(validPacket(0));
  b[4] = 2;
  OLV_CHECK_EQ(run(b), proto::DecodeError::kBadVersion);
}

OLV_TEST(decode_bad_msg_type) {
  auto b = proto::encode(validPacket(0));
  b[5] = 2;
  OLV_CHECK_EQ(run(b), proto::DecodeError::kBadMsgType);
}

OLV_TEST(decode_object_count_over_128) {
  // 60-byte datagram (in range) but count field claims 129 -> kTooManyObjects
  // (checked before the length rule).
  auto b = proto::encode(validPacket(0));
  setU16(b, 52, 129);
  OLV_CHECK_EQ(run(b), proto::DecodeError::kTooManyObjects);
}

OLV_TEST(decode_count_greater_than_total) {
  auto b = proto::encode(validPacket(2));  // count=2,total=2
  setU16(b, 54, 1);                        // total=1 < count -> kTooManyObjects
  OLV_CHECK_EQ(run(b), proto::DecodeError::kTooManyObjects);
}

OLV_TEST(decode_bad_length) {
  // count field says 1 but no record present (length stays 60).
  proto::StatePacket pkt;
  pkt.object_total = 1;  // total=1 so count<=total holds
  auto b = proto::encode(pkt);
  setU16(b, 52, 1);  // count=1, but datagram is only 60 bytes
  OLV_CHECK_EQ(run(b), proto::DecodeError::kBadLength);
}

OLV_TEST(decode_bad_crc) {
  auto b = proto::encode(validPacket(1));
  b[16] ^= 0xFF;  // corrupt a satellite position byte, leave CRC stale
  OLV_CHECK_EQ(run(b), proto::DecodeError::kBadCrc);
}

OLV_TEST(decode_non_finite_satellite) {
  auto pkt = validPacket(0);
  pkt.sat_px = std::numeric_limits<double>::quiet_NaN();
  auto b = proto::encode(pkt);  // CRC computed over the NaN bytes
  OLV_CHECK_EQ(run(b), proto::DecodeError::kNonFinite);
}

OLV_TEST(decode_non_finite_object_velocity) {
  auto pkt = validPacket(1);
  pkt.objects[0].vx = std::numeric_limits<float>::quiet_NaN();
  auto b = proto::encode(pkt);
  OLV_CHECK_EQ(run(b), proto::DecodeError::kNonFinite);
}

OLV_TEST(decode_out_of_range_position) {
  auto pkt = validPacket(0);
  pkt.sat_px = 1.1e13;  // > kMaxCoordinateMeters
  auto b = proto::encode(pkt);
  OLV_CHECK_EQ(run(b), proto::DecodeError::kOutOfRange);
}

OLV_TEST(decode_bad_object_type_zero) {
  auto pkt = validPacket(1);
  pkt.objects[0].type = 0;
  auto b = proto::encode(pkt);
  OLV_CHECK_EQ(run(b), proto::DecodeError::kBadObjectType);
}

OLV_TEST(decode_bad_object_type_six) {
  auto pkt = validPacket(1);
  pkt.objects[0].type = 6;
  auto b = proto::encode(pkt);
  OLV_CHECK_EQ(run(b), proto::DecodeError::kBadObjectType);
}

OLV_TEST(decode_mutate_then_refresh_crc_reaches_range_check) {
  // Demonstrates the mutate+recompute-CRC pattern: patch a position to an
  // out-of-range value directly in the bytes, refresh CRC so decode gets past
  // the CRC gate to the range check.
  auto b = proto::encode(validPacket(1));
  // Object 0 pos_x is at offset kHeaderSize + 8.
  const std::size_t px_off = proto::kHeaderSize + 8;
  const double bad = 2e13;
  std::uint64_t bits;
  std::memcpy(&bits, &bad, sizeof(bits));
  for (int i = 0; i < 8; ++i) b[px_off + i] = static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFF);
  refreshCrc(b);
  OLV_CHECK_EQ(run(b), proto::DecodeError::kOutOfRange);
}
