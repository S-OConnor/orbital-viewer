// test_protocol.cpp — CRC vector, sizes, encode/decode round-trip, truncation.

#include <cstdint>
#include <ostream>
#include <vector>

#include <gtest/gtest.h>

#include "olv/protocol.hpp"

using namespace olv;

// Streamable for GoogleTest's failure messages (ADL in olv::proto).
namespace olv::proto {
inline std::ostream& operator<<(std::ostream& os, DecodeError e) {
  return os << toString(e);
}
}  // namespace olv::proto

namespace {

proto::ObjectRecord makeObj(std::uint32_t id, proto::ObjectType type, std::uint8_t flags,
                            std::uint8_t conf, double px, double py, double pz, float vx, float vy,
                            float vz, float intensity) {
  proto::ObjectRecord r;
  r.id = id;
  r.type = static_cast<std::uint8_t>(type);
  r.flags = flags;
  r.confidence = conf;
  r.px = px;
  r.py = py;
  r.pz = pz;
  r.vx = vx;
  r.vy = vy;
  r.vz = vz;
  r.intensity = intensity;
  return r;
}

void checkObjEq(const proto::ObjectRecord& a, const proto::ObjectRecord& b) {
  EXPECT_EQ(a.id, b.id);
  EXPECT_EQ(a.type, b.type);
  EXPECT_EQ(a.flags, b.flags);
  EXPECT_EQ(a.confidence, b.confidence);
  EXPECT_EQ(a.px, b.px);
  EXPECT_EQ(a.py, b.py);
  EXPECT_EQ(a.pz, b.pz);
  EXPECT_EQ(a.vx, b.vx);
  EXPECT_EQ(a.vy, b.vy);
  EXPECT_EQ(a.vz, b.vz);
  EXPECT_EQ(a.intensity, b.intensity);
}

}  // namespace

TEST(Protocol, crc32_reference_vector) {
  const char* s = "123456789";
  EXPECT_EQ(proto::crc32(reinterpret_cast<const std::uint8_t*>(s), 9), 0xCBF43926u);
}

TEST(Protocol, empty_packet_size_is_60) {
  proto::StatePacket pkt;
  pkt.sequence = 1;
  const auto bytes = proto::encode(pkt);
  EXPECT_EQ(bytes.size(), std::size_t{60});
  EXPECT_EQ(bytes.size(), proto::kHeaderSize + proto::kCrcSize);
}

TEST(Protocol, roundtrip_three_objects) {
  proto::StatePacket pkt;
  pkt.sequence = 42;
  pkt.sat_id = 7;
  pkt.sat_px = 4126540.2;
  pkt.sat_py = -4681712.5;
  pkt.sat_pz = 1003432.1;
  pkt.sat_vx = 1234.56f;
  pkt.sat_vy = 2345.67f;
  pkt.sat_vz = -6543.21f;
  pkt.object_total = 3;

  // Debris with velocity, normal confidence.
  pkt.objects.push_back(makeObj(1001, proto::ObjectType::kDebris, proto::kFlagHasVelocity, 87,
                                6923371.4, 12000.0, -55000.2, 7611.0f, 0.0f, 0.0f, 0.0f));
  // Star, highlight, no velocity, confidence 255 -> clamped to 100, magnitude.
  pkt.objects.push_back(makeObj(2001, proto::ObjectType::kStar, proto::kFlagHighlight, 255, 1e12,
                                -2e12, 3e12, 0.0f, 0.0f, 0.0f, 4.5f));
  // Ground-hot, velocity + highlight, temperature intensity.
  pkt.objects.push_back(makeObj(3001, proto::ObjectType::kGroundHot,
                                proto::kFlagHasVelocity | proto::kFlagHighlight, 50, 1113194.9,
                                -4842330.0, 3985029.2, 1.0f, -2.0f, 3.0f, 1450.0f));

  const auto bytes = proto::encode(pkt);
  EXPECT_EQ(bytes.size(), proto::kHeaderSize + 3 * proto::kRecordSize + proto::kCrcSize);

  proto::StatePacket dec;
  EXPECT_EQ(proto::decode(bytes.data(), bytes.size(), dec), proto::DecodeError::kNone);
  EXPECT_EQ(dec.sequence, pkt.sequence);
  EXPECT_EQ(dec.sat_id, pkt.sat_id);
  EXPECT_EQ(dec.sat_px, pkt.sat_px);
  EXPECT_EQ(dec.sat_py, pkt.sat_py);
  EXPECT_EQ(dec.sat_pz, pkt.sat_pz);
  EXPECT_EQ(dec.sat_vx, pkt.sat_vx);
  EXPECT_EQ(dec.sat_vy, pkt.sat_vy);
  EXPECT_EQ(dec.sat_vz, pkt.sat_vz);
  EXPECT_EQ(dec.object_total, std::uint16_t{3});
  EXPECT_EQ(dec.objects.size(), std::size_t{3});

  // confidence 255 was clamped to 100 on encode.
  proto::ObjectRecord expected_star = pkt.objects[1];
  expected_star.confidence = 100;
  checkObjEq(dec.objects[0], pkt.objects[0]);
  checkObjEq(dec.objects[1], expected_star);
  checkObjEq(dec.objects[2], pkt.objects[2]);

  EXPECT_TRUE(dec.objects[0].hasVelocity());
  EXPECT_FALSE(dec.objects[1].hasVelocity());
  EXPECT_TRUE(dec.objects[2].hasVelocity());
}

TEST(Protocol, max_packet_128_objects) {
  proto::StatePacket pkt;
  pkt.sequence = 5;
  pkt.object_total = proto::kMaxObjectsPerPacket;
  for (std::uint16_t i = 0; i < proto::kMaxObjectsPerPacket; ++i) {
    pkt.objects.push_back(makeObj(1000 + i, proto::ObjectType::kSatellite, proto::kFlagHasVelocity,
                                  10, static_cast<double>(i), 0.0, 0.0, 1.0f, 2.0f, 3.0f, 0.0f));
  }
  const auto bytes = proto::encode(pkt);
  EXPECT_EQ(bytes.size(), std::size_t{6204});
  EXPECT_EQ(bytes.size(), proto::kMaxPacketSize);

  proto::StatePacket dec;
  EXPECT_EQ(proto::decode(bytes.data(), bytes.size(), dec), proto::DecodeError::kNone);
  EXPECT_EQ(dec.objects.size(), std::size_t{128});
}

TEST(Protocol, encode_truncates_above_128) {
  proto::StatePacket pkt;
  pkt.sequence = 9;
  pkt.object_total = 200;
  for (int i = 0; i < 200; ++i) {
    pkt.objects.push_back(
        makeObj(1, proto::ObjectType::kDebris, 0, 0, 0.0, 0.0, 0.0, 0.0f, 0.0f, 0.0f, 0.0f));
  }
  const auto bytes = proto::encode(pkt);
  EXPECT_EQ(bytes.size(), std::size_t{6204});

  proto::StatePacket dec;
  EXPECT_EQ(proto::decode(bytes.data(), bytes.size(), dec), proto::DecodeError::kNone);
  EXPECT_EQ(dec.objects.size(), std::size_t{128});
  EXPECT_EQ(dec.object_total, std::uint16_t{200});
}
