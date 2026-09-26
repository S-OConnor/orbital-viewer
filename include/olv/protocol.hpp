// olv/protocol.hpp — OLV1 UDP wire protocol, version 1 (header-only).
//
// Single source of truth for the packet layout documented in
// docs/PROTOCOL_UDP.md. All multi-byte fields are little-endian; layout is
// defined by byte offset and serialized field-by-field (no struct overlay),
// so the code is portable across alignment/endianness.
//
// Shared by olv_backend, olv_sim, and the unit tests.

#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace olv::proto {

// ---------------------------------------------------------------------------
// Constants & limits (see docs/PROTOCOL_UDP.md §2)
// ---------------------------------------------------------------------------

inline constexpr std::array<std::uint8_t, 4> kMagic{'O', 'L', 'V', '1'};
inline constexpr std::uint8_t kVersion = 1;

inline constexpr std::size_t kHeaderSize = 56;
inline constexpr std::size_t kRecordSize = 48;
inline constexpr std::size_t kCrcSize = 4;
inline constexpr std::uint16_t kMaxObjectsPerPacket = 128;
inline constexpr std::uint16_t kMaxTrackedObjects = 5000;
inline constexpr std::size_t kMinPacketSize = kHeaderSize + kCrcSize;
inline constexpr std::size_t kMaxPacketSize =
    kHeaderSize + std::size_t{kMaxObjectsPerPacket} * kRecordSize + kCrcSize;  // 6204

// Sanity bound for any ECEF coordinate component. Stars are encoded as
// distant direction markers (|pos| ~1e12 m), so the bound is generous.
inline constexpr double kMaxCoordinateMeters = 1e13;

enum class MsgType : std::uint8_t { kStateUpdate = 1 };

// kUnknown is the catch-all: a sender that does not know an object's type
// sends 0, and any unrecognized type byte decodes as kUnknown too.
enum class ObjectType : std::uint8_t {
  kUnknown = 0,
  kDebris = 1,
  kStar = 2,
  kComet = 3,
  kSatellite = 4,
  kGroundHot = 5,
};

inline constexpr bool isKnownObjectType(std::uint8_t t) {
  return t <= 5;
}

// Maps any unrecognized type byte to kUnknown; known values pass through.
inline constexpr std::uint8_t normalizeObjectType(std::uint8_t t) {
  return isKnownObjectType(t) ? t : static_cast<std::uint8_t>(ObjectType::kUnknown);
}

inline const char* toString(ObjectType t) {
  switch (t) {
    case ObjectType::kUnknown:
      return "unknown";
    case ObjectType::kDebris:
      return "debris";
    case ObjectType::kStar:
      return "star";
    case ObjectType::kComet:
      return "comet";
    case ObjectType::kSatellite:
      return "satellite";
    case ObjectType::kGroundHot:
      return "ground_hot";
  }
  return "unknown";
}

// Object record flag bits.
inline constexpr std::uint8_t kFlagHasVelocity = 0x01;
inline constexpr std::uint8_t kFlagHighlight = 0x02;

// ---------------------------------------------------------------------------
// Decoded message types
// ---------------------------------------------------------------------------

struct ObjectRecord {
  std::uint32_t id = 0;
  std::uint8_t type = 0;                  // ObjectType value
  std::uint8_t flags = 0;                 // kFlag* bits
  std::uint8_t confidence = 0;            // 0..100 (%), clamped on decode
  double px = 0.0, py = 0.0, pz = 0.0;    // ECEF meters
  float vx = 0.0f, vy = 0.0f, vz = 0.0f;  // ECEF m/s, valid iff kFlagHasVelocity
  float intensity = 0.0f;                 // Kelvin (ground-hot), magnitude (star), else 0

  bool hasVelocity() const { return (flags & kFlagHasVelocity) != 0; }
};

struct StatePacket {
  std::uint32_t sequence = 0;  // increments per packet sent (wraps)
  std::uint32_t sat_id = 0;
  double sat_px = 0.0, sat_py = 0.0, sat_pz = 0.0;    // ECEF meters
  float sat_vx = 0.0f, sat_vy = 0.0f, sat_vz = 0.0f;  // ECEF m/s
  std::uint16_t object_total = 0;                     // objects in the full update cycle
  std::vector<ObjectRecord> objects;                  // <= kMaxObjectsPerPacket
};

enum class DecodeError {
  kNone = 0,
  kTooShort,
  kTooLong,
  kBadMagic,
  kBadVersion,
  kBadMsgType,
  kTooManyObjects,
  kBadLength,
  kBadCrc,
  kNonFinite,
  kOutOfRange,
};

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
    case DecodeError::kTooManyObjects:
      return "too_many_objects";
    case DecodeError::kBadLength:
      return "bad_length";
    case DecodeError::kBadCrc:
      return "bad_crc";
    case DecodeError::kNonFinite:
      return "non_finite";
    case DecodeError::kOutOfRange:
      return "out_of_range";
  }
  return "unknown";
}

// ---------------------------------------------------------------------------
// CRC-32 (IEEE 802.3 / zlib): reflected, poly 0x04C11DB7 (0xEDB88320
// reflected), init 0xFFFFFFFF, final XOR 0xFFFFFFFF.
// ---------------------------------------------------------------------------

namespace detail {

inline constexpr std::array<std::uint32_t, 256> makeCrcTable() {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t i = 0; i < 256; ++i) {
    std::uint32_t c = i;
    for (int k = 0; k < 8; ++k) {
      c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    }
    table[i] = c;
  }
  return table;
}

inline constexpr std::array<std::uint32_t, 256> kCrcTable = makeCrcTable();

// --- Little-endian byte I/O helpers -------------------------------------

inline void putU16(std::vector<std::uint8_t>& out, std::uint16_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

inline void putU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

inline void putU64(std::vector<std::uint8_t>& out, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
  }
}

inline void putF32(std::vector<std::uint8_t>& out, float v) {
  putU32(out, std::bit_cast<std::uint32_t>(v));
}

inline void putF64(std::vector<std::uint8_t>& out, double v) {
  putU64(out, std::bit_cast<std::uint64_t>(v));
}

inline std::uint16_t getU16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0] | (std::uint16_t{p[1]} << 8));
}

inline std::uint32_t getU32(const std::uint8_t* p) {
  return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16) |
         (std::uint32_t{p[3]} << 24);
}

inline std::uint64_t getU64(const std::uint8_t* p) {
  std::uint64_t v = 0;
  for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
  return v;
}

inline float getF32(const std::uint8_t* p) {
  return std::bit_cast<float>(getU32(p));
}
inline double getF64(const std::uint8_t* p) {
  return std::bit_cast<double>(getU64(p));
}

inline bool finiteAll(std::initializer_list<double> vs) {
  for (double v : vs) {
    if (!std::isfinite(v)) return false;
  }
  return true;
}

inline bool inCoordRange(double x, double y, double z) {
  return std::fabs(x) <= kMaxCoordinateMeters && std::fabs(y) <= kMaxCoordinateMeters &&
         std::fabs(z) <= kMaxCoordinateMeters;
}

}  // namespace detail

inline std::uint32_t crc32(const std::uint8_t* data, std::size_t len) {
  std::uint32_t c = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < len; ++i) {
    c = detail::kCrcTable[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
  }
  return c ^ 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
// encode: StatePacket -> datagram bytes.
//
// Enforces limits by truncation/clamping (callers are first-party):
// objects beyond kMaxObjectsPerPacket are dropped, confidence is clamped to
// 100, object_total is clamped to kMaxTrackedObjects.
// ---------------------------------------------------------------------------

inline std::vector<std::uint8_t> encode(const StatePacket& pkt) {
  const std::size_t count =
      pkt.objects.size() > kMaxObjectsPerPacket ? kMaxObjectsPerPacket : pkt.objects.size();
  std::vector<std::uint8_t> out;
  out.reserve(kHeaderSize + count * kRecordSize + kCrcSize);

  out.insert(out.end(), kMagic.begin(), kMagic.end());
  out.push_back(kVersion);
  out.push_back(static_cast<std::uint8_t>(MsgType::kStateUpdate));
  detail::putU16(out, 0);  // hdr_flags (reserved)
  detail::putU32(out, pkt.sequence);
  detail::putU32(out, pkt.sat_id);
  detail::putF64(out, pkt.sat_px);
  detail::putF64(out, pkt.sat_py);
  detail::putF64(out, pkt.sat_pz);
  detail::putF32(out, pkt.sat_vx);
  detail::putF32(out, pkt.sat_vy);
  detail::putF32(out, pkt.sat_vz);
  detail::putU16(out, static_cast<std::uint16_t>(count));
  detail::putU16(out,
                 pkt.object_total > kMaxTrackedObjects ? kMaxTrackedObjects : pkt.object_total);

  for (std::size_t i = 0; i < count; ++i) {
    const ObjectRecord& r = pkt.objects[i];
    detail::putU32(out, r.id);
    out.push_back(r.type);
    out.push_back(r.flags);
    out.push_back(r.confidence > 100 ? std::uint8_t{100} : r.confidence);
    out.push_back(0);  // reserved
    detail::putF64(out, r.px);
    detail::putF64(out, r.py);
    detail::putF64(out, r.pz);
    detail::putF32(out, r.vx);
    detail::putF32(out, r.vy);
    detail::putF32(out, r.vz);
    detail::putF32(out, r.intensity);
  }

  detail::putU32(out, crc32(out.data(), out.size()));
  return out;
}

// ---------------------------------------------------------------------------
// decode: datagram bytes -> StatePacket.
//
// Performs the full validation sequence from docs/PROTOCOL_UDP.md §4 items
// 1-10 (structural, CRC, finiteness, range, enum). Item 11 (sequence
// staleness) is stateful and lives in StateStore::apply. Returns kNone on
// success; `out` is only valid in that case.
// ---------------------------------------------------------------------------

inline DecodeError decode(const std::uint8_t* data, std::size_t len, StatePacket& out) {
  if (len < kMinPacketSize) return DecodeError::kTooShort;
  if (len > kMaxPacketSize) return DecodeError::kTooLong;
  if (std::memcmp(data, kMagic.data(), kMagic.size()) != 0) return DecodeError::kBadMagic;
  if (data[4] != kVersion) return DecodeError::kBadVersion;
  if (data[5] != static_cast<std::uint8_t>(MsgType::kStateUpdate)) {
    return DecodeError::kBadMsgType;
  }

  const std::uint16_t count = detail::getU16(data + 52);
  const std::uint16_t total = detail::getU16(data + 54);
  if (count > kMaxObjectsPerPacket || total > kMaxTrackedObjects || count > total) {
    return DecodeError::kTooManyObjects;
  }
  if (len != kHeaderSize + std::size_t{count} * kRecordSize + kCrcSize) {
    return DecodeError::kBadLength;
  }
  const std::uint32_t wire_crc = detail::getU32(data + len - kCrcSize);
  if (crc32(data, len - kCrcSize) != wire_crc) return DecodeError::kBadCrc;

  StatePacket pkt;
  pkt.sequence = detail::getU32(data + 8);
  pkt.sat_id = detail::getU32(data + 12);
  pkt.sat_px = detail::getF64(data + 16);
  pkt.sat_py = detail::getF64(data + 24);
  pkt.sat_pz = detail::getF64(data + 32);
  pkt.sat_vx = detail::getF32(data + 40);
  pkt.sat_vy = detail::getF32(data + 44);
  pkt.sat_vz = detail::getF32(data + 48);
  pkt.object_total = total;

  if (!detail::finiteAll({pkt.sat_px, pkt.sat_py, pkt.sat_pz, static_cast<double>(pkt.sat_vx),
                          static_cast<double>(pkt.sat_vy), static_cast<double>(pkt.sat_vz)})) {
    return DecodeError::kNonFinite;
  }
  if (!detail::inCoordRange(pkt.sat_px, pkt.sat_py, pkt.sat_pz)) {
    return DecodeError::kOutOfRange;
  }

  pkt.objects.reserve(count);
  for (std::uint16_t i = 0; i < count; ++i) {
    const std::uint8_t* p = data + kHeaderSize + std::size_t{i} * kRecordSize;
    ObjectRecord r;
    r.id = detail::getU32(p + 0);
    r.type = normalizeObjectType(p[4]);
    r.flags = p[5];
    r.confidence = p[6] > 100 ? std::uint8_t{100} : p[6];
    r.px = detail::getF64(p + 8);
    r.py = detail::getF64(p + 16);
    r.pz = detail::getF64(p + 24);
    r.vx = detail::getF32(p + 32);
    r.vy = detail::getF32(p + 36);
    r.vz = detail::getF32(p + 40);
    r.intensity = detail::getF32(p + 44);

    if (!detail::finiteAll({r.px, r.py, r.pz, static_cast<double>(r.vx), static_cast<double>(r.vy),
                            static_cast<double>(r.vz), static_cast<double>(r.intensity)})) {
      return DecodeError::kNonFinite;
    }
    if (!detail::inCoordRange(r.px, r.py, r.pz)) return DecodeError::kOutOfRange;
    pkt.objects.push_back(r);
  }

  out = std::move(pkt);
  return DecodeError::kNone;
}

}  // namespace olv::proto
