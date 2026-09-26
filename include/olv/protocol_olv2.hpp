// olv/protocol_olv2.hpp — OLV2 UDP wire protocol, version 1 (header-only).
//
// Single source of truth for the packet layout documented in
// docs/PROTOCOL_OLV2.md. One datagram carries one target track: 1..25
// time-stamped points, each holding the satellite (ownship) and target
// state at that instant. Batches are disjoint — a sender never repeats a
// point in a later datagram (docs/features/FEATURE_OLV2.md D3).
//
// Byte order, alignment, float formats, and the CRC-32 are identical to OLV1:
// this header reuses proto::crc32, the proto::detail byte helpers, and the
// ObjectType enum from olv/protocol.hpp (which it does not modify).
//
// Shared by olv_backend, olv_sim, and the unit tests.

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "olv/protocol.hpp"

namespace olv::proto::olv2 {

// ---------------------------------------------------------------------------
// Constants & limits (see docs/PROTOCOL_OLV2.md §2)
// ---------------------------------------------------------------------------

inline constexpr std::array<std::uint8_t, 4> kMagic{'O', 'L', 'V', '2'};
inline constexpr std::uint8_t kVersion = 1;

inline constexpr std::size_t kHeaderSize = 24;
inline constexpr std::size_t kPointSize = 84;
inline constexpr std::size_t kCrcSize = 4;
inline constexpr std::uint8_t kMaxPoints = 25;
inline constexpr std::size_t kMinPacketSize = kHeaderSize + kPointSize + kCrcSize;  // 112
inline constexpr std::size_t kMaxPacketSize =
    kHeaderSize + std::size_t{kMaxPoints} * kPointSize + kCrcSize;  // 2128

enum class MsgType : std::uint8_t { kTrackUpdate = 1 };

// Point flag bits.
inline constexpr std::uint8_t kPointSatHasVel = 0x01;
inline constexpr std::uint8_t kPointTgtHasVel = 0x02;

// target_flags uses the OLV1 object flag bit positions; only HIGHLIGHT is
// meaningful on the wire (has-velocity is per point, above).

// ---------------------------------------------------------------------------
// Decoded message types
// ---------------------------------------------------------------------------

struct Point {
  double t = 0.0;          // UTC seconds since the Unix epoch
  std::uint8_t flags = 0;  // kPoint* bits
  double sat_px = 0.0, sat_py = 0.0, sat_pz = 0.0;    // ECEF meters
  double tgt_px = 0.0, tgt_py = 0.0, tgt_pz = 0.0;    // ECEF meters
  float sat_vx = 0.0f, sat_vy = 0.0f, sat_vz = 0.0f;  // ECEF m/s, valid iff kPointSatHasVel
  float tgt_vx = 0.0f, tgt_vy = 0.0f, tgt_vz = 0.0f;  // ECEF m/s, valid iff kPointTgtHasVel

  bool satHasVelocity() const { return (flags & kPointSatHasVel) != 0; }
  bool tgtHasVelocity() const { return (flags & kPointTgtHasVel) != 0; }
};

struct TrackPacket {
  std::uint32_t sequence = 0;      // per-datagram sender counter (diagnostic only)
  std::uint32_t track_id = 0;      // target id
  std::uint32_t sat_id = 0;        // satellite (ownship) id
  std::uint8_t target_type = 0;    // ObjectType value
  std::uint8_t target_flags = 0;   // proto::kFlagHighlight only
  std::uint8_t confidence = 0;     // 0..100 (%), clamped on decode
  std::vector<Point> points;       // 1..kMaxPoints, strictly ascending t
};

enum class DecodeError {
  kNone = 0,
  kTooShort,
  kTooLong,
  kBadMagic,
  kBadVersion,
  kBadMsgType,
  kBadPointCount,
  kBadLength,
  kBadCrc,
  kNonFinite,
  kOutOfRange,
  kBadTime,
};

// Strings match OLV1's proto::toString(DecodeError) where the reason is shared.
inline const char* toString(DecodeError e) {
  switch (e) {
    case DecodeError::kNone:
      return "none";
    case DecodeError::kTooShort:
      return "too_short";
    case DecodeError::kTooLong:
      return "too_long";
    case DecodeError::kBadMagic:
      return "bad_magic";
    case DecodeError::kBadVersion:
      return "bad_version";
    case DecodeError::kBadMsgType:
      return "bad_msg_type";
    case DecodeError::kBadPointCount:
      return "bad_point_count";
    case DecodeError::kBadLength:
      return "bad_length";
    case DecodeError::kBadCrc:
      return "bad_crc";
    case DecodeError::kNonFinite:
      return "non_finite";
    case DecodeError::kOutOfRange:
      return "out_of_range";
    case DecodeError::kBadTime:
      return "bad_time";
  }
  return "unknown";
}

// ---------------------------------------------------------------------------
// encode: TrackPacket -> datagram bytes.
//
// Enforces limits by truncation/clamping (callers are first-party): points
// beyond kMaxPoints are dropped, confidence is clamped to 100, target_flags
// is masked to HIGHLIGHT, point flags to the two defined bits. Does not
// validate time ordering — the receiver does (kBadTime).
// ---------------------------------------------------------------------------

inline std::vector<std::uint8_t> encode(const TrackPacket& pkt) {
  const std::size_t count = pkt.points.size() > kMaxPoints ? kMaxPoints : pkt.points.size();
  std::vector<std::uint8_t> out;
  out.reserve(kHeaderSize + count * kPointSize + kCrcSize);

  out.insert(out.end(), kMagic.begin(), kMagic.end());
  out.push_back(kVersion);
  out.push_back(static_cast<std::uint8_t>(MsgType::kTrackUpdate));
  detail::putU16(out, 0);  // hdr_flags (reserved)
  detail::putU32(out, pkt.sequence);
  detail::putU32(out, pkt.track_id);
  detail::putU32(out, pkt.sat_id);
  out.push_back(pkt.target_type);
  out.push_back(static_cast<std::uint8_t>(pkt.target_flags & kFlagHighlight));
  out.push_back(pkt.confidence > 100 ? std::uint8_t{100} : pkt.confidence);
  out.push_back(static_cast<std::uint8_t>(count));

  for (std::size_t i = 0; i < count; ++i) {
    const Point& p = pkt.points[i];
    detail::putF64(out, p.t);
    out.push_back(static_cast<std::uint8_t>(p.flags & (kPointSatHasVel | kPointTgtHasVel)));
    out.push_back(0);  // reserved
    out.push_back(0);
    out.push_back(0);
    detail::putF64(out, p.sat_px);
    detail::putF64(out, p.sat_py);
    detail::putF64(out, p.sat_pz);
    detail::putF64(out, p.tgt_px);
    detail::putF64(out, p.tgt_py);
    detail::putF64(out, p.tgt_pz);
    detail::putF32(out, p.sat_vx);
    detail::putF32(out, p.sat_vy);
    detail::putF32(out, p.sat_vz);
    detail::putF32(out, p.tgt_vx);
    detail::putF32(out, p.tgt_vy);
    detail::putF32(out, p.tgt_vz);
  }

  detail::putU32(out, crc32(out.data(), out.size()));
  return out;
}

// ---------------------------------------------------------------------------
// decode: datagram bytes -> TrackPacket.
//
// Performs the full validation sequence from docs/PROTOCOL_OLV2.md §4 items
// 1-10 (structural, CRC, finiteness, range, time ordering). Item 11 (per-track
// staleness) is stateful and lives in Olv2InputSource. Returns kNone on
// success; `out` is only valid in that case.
// ---------------------------------------------------------------------------

inline DecodeError decode(const std::uint8_t* data, std::size_t len, TrackPacket& out) {
  if (len < kMinPacketSize) return DecodeError::kTooShort;
  if (len > kMaxPacketSize) return DecodeError::kTooLong;
  if (std::memcmp(data, kMagic.data(), kMagic.size()) != 0) return DecodeError::kBadMagic;
  if (data[4] != kVersion) return DecodeError::kBadVersion;
  if (data[5] != static_cast<std::uint8_t>(MsgType::kTrackUpdate)) {
    return DecodeError::kBadMsgType;
  }

  const std::uint8_t count = data[23];
  if (count == 0 || count > kMaxPoints) return DecodeError::kBadPointCount;
  if (len != kHeaderSize + std::size_t{count} * kPointSize + kCrcSize) {
    return DecodeError::kBadLength;
  }
  const std::uint32_t wire_crc = detail::getU32(data + len - kCrcSize);
  if (crc32(data, len - kCrcSize) != wire_crc) return DecodeError::kBadCrc;

  TrackPacket pkt;
  pkt.sequence = detail::getU32(data + 8);
  pkt.track_id = detail::getU32(data + 12);
  pkt.sat_id = detail::getU32(data + 16);
  pkt.target_type = normalizeObjectType(data[20]);
  pkt.target_flags = static_cast<std::uint8_t>(data[21] & kFlagHighlight);
  pkt.confidence = data[22] > 100 ? std::uint8_t{100} : data[22];

  // Items 8 and 9 are checked for every point before item 10, so a packet
  // that is both non-finite and out of order reports the earlier item.
  pkt.points.reserve(count);
  for (std::uint8_t i = 0; i < count; ++i) {
    const std::uint8_t* q = data + kHeaderSize + std::size_t{i} * kPointSize;
    Point p;
    p.t = detail::getF64(q + 0);
    p.flags = static_cast<std::uint8_t>(q[8] & (kPointSatHasVel | kPointTgtHasVel));
    p.sat_px = detail::getF64(q + 12);
    p.sat_py = detail::getF64(q + 20);
    p.sat_pz = detail::getF64(q + 28);
    p.tgt_px = detail::getF64(q + 36);
    p.tgt_py = detail::getF64(q + 44);
    p.tgt_pz = detail::getF64(q + 52);
    p.sat_vx = detail::getF32(q + 60);
    p.sat_vy = detail::getF32(q + 64);
    p.sat_vz = detail::getF32(q + 68);
    p.tgt_vx = detail::getF32(q + 72);
    p.tgt_vy = detail::getF32(q + 76);
    p.tgt_vz = detail::getF32(q + 80);

    if (!detail::finiteAll({p.t, p.sat_px, p.sat_py, p.sat_pz, p.tgt_px, p.tgt_py, p.tgt_pz,
                            static_cast<double>(p.sat_vx), static_cast<double>(p.sat_vy),
                            static_cast<double>(p.sat_vz), static_cast<double>(p.tgt_vx),
                            static_cast<double>(p.tgt_vy), static_cast<double>(p.tgt_vz)})) {
      return DecodeError::kNonFinite;
    }
    if (!detail::inCoordRange(p.sat_px, p.sat_py, p.sat_pz) ||
        !detail::inCoordRange(p.tgt_px, p.tgt_py, p.tgt_pz)) {
      return DecodeError::kOutOfRange;
    }
    pkt.points.push_back(p);
  }

  for (std::size_t i = 0; i < pkt.points.size(); ++i) {
    if (pkt.points[i].t <= 0.0) return DecodeError::kBadTime;
    if (i > 0 && pkt.points[i].t <= pkt.points[i - 1].t) return DecodeError::kBadTime;
  }

  out = std::move(pkt);
  return DecodeError::kNone;
}

}  // namespace olv::proto::olv2
