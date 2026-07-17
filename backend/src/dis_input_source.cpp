// dis_input_source.cpp — see dis_input_source.hpp. Thread: private io_context
// on an internal std::thread running the async receive loop; each datagram is
// translated from a DIS Entity State PDU into a StateStore StatePacket per the
// frozen §3 decisions.

#include "dis_input_source.hpp"

#include <dis6/EntityID.h>
#include <dis6/EntityStatePdu.h>
#include <dis6/EntityType.h>
#include <dis6/Vector3Double.h>
#include <dis6/Vector3Float.h>
#include <dis6/utils/DataStream.h>
#include <dis6/utils/Endian.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <string>

#include "logger.hpp"
#include "olv/protocol.hpp"

namespace olv {

namespace asio = boost::asio;
using asio::ip::udp;

namespace {

// DIS PDU header: protocolVersion(1) exerciseID(1) pduType(1) protocolFamily(1)
// timestamp(4, big-endian) length(2) padding(2) = 12 bytes (IEEE 1278.1 §5.2.24).
constexpr std::size_t kDisHeaderSize = 12;
constexpr std::uint8_t kEntityStatePduType = 1;
constexpr std::uint8_t kMinProtocolVersion = 5;  // DIS-1995
constexpr std::uint8_t kMaxProtocolVersion = 7;  // accepted base-field-compatible

std::uint32_t beU32(const std::uint8_t* p) {
  return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) |
         std::uint32_t{p[3]};
}

std::uint64_t pack48(std::uint16_t site, std::uint16_t app, std::uint16_t entity) {
  return (std::uint64_t{site} << 32) | (std::uint64_t{app} << 16) | std::uint64_t{entity};
}

void unpack48(std::uint64_t packed, std::uint16_t& site, std::uint16_t& app,
              std::uint16_t& entity) {
  site = static_cast<std::uint16_t>((packed >> 32) & 0xFFFF);
  app = static_cast<std::uint16_t>((packed >> 16) & 0xFFFF);
  entity = static_cast<std::uint16_t>(packed & 0xFFFF);
}

// FNV-1a 32-bit over the six big-endian EntityID bytes (§3.3): spreads the
// typical "same site/app, sequential entity" pattern across the 32-bit id
// space better than bit-packing truncation.
std::uint32_t foldEntityId(std::uint16_t site, std::uint16_t app, std::uint16_t entity) {
  const std::uint8_t bytes[6] = {
      static_cast<std::uint8_t>(site >> 8),   static_cast<std::uint8_t>(site & 0xFF),
      static_cast<std::uint8_t>(app >> 8),    static_cast<std::uint8_t>(app & 0xFF),
      static_cast<std::uint8_t>(entity >> 8), static_cast<std::uint8_t>(entity & 0xFF)};
  std::uint32_t h = 2166136261u;  // FNV offset basis
  for (std::uint8_t b : bytes) {
    h ^= b;
    h *= 16777619u;  // FNV prime
  }
  return h;
}

// DIS EntityType (kind, domain) -> OLV ObjectType (§3.4). Exhaustive by
// construction via the kDebris fallback; never an error.
std::uint8_t mapObjectType(std::uint8_t kind, std::uint8_t domain) {
  using proto::ObjectType;
  if (kind == 1 && domain == 5) return static_cast<std::uint8_t>(ObjectType::kSatellite);
  if (kind == 1 && domain == 1) return static_cast<std::uint8_t>(ObjectType::kGroundHot);
  if (kind == 3 && domain == 1) return static_cast<std::uint8_t>(ObjectType::kGroundHot);
  if (kind == 2) return static_cast<std::uint8_t>(ObjectType::kComet);
  return static_cast<std::uint8_t>(ObjectType::kDebris);
}

std::string entityIdString(std::uint16_t site, std::uint16_t app, std::uint16_t entity) {
  return std::to_string(site) + ":" + std::to_string(app) + ":" + std::to_string(entity);
}

}  // namespace

DisInputSource::DisInputSource(const DisInputConfig& cfg, StateStore& store, Logger& log)
    : cfg_(cfg),
      socket_(ioc_, udp::endpoint(asio::ip::make_address(cfg.bind_address), cfg.port)),
      store_(store),
      log_(log) {
  if (!parseDisEntityId(cfg_.satellite_entity_id, sat_site_, sat_app_, sat_entity_)) {
    throw std::invalid_argument("invalid satellite_entity_id \"" + cfg_.satellite_entity_id +
                                "\" (expected site:application:entity)");
  }
}

DisInputSource::~DisInputSource() {
  stop();
}

void DisInputSource::start() {
  if (started_) return;
  started_ = true;
  armReceive();
  thread_ = std::thread([this] { ioc_.run(); });
}

void DisInputSource::stop() {
  if (!started_) return;
  asio::post(ioc_, [this] {
    boost::system::error_code ec;
    socket_.close(ec);
    ioc_.stop();
  });
  if (thread_.joinable()) thread_.join();
  started_ = false;
}

void DisInputSource::armReceive() {
  socket_.async_receive_from(
      asio::buffer(buffer_), sender_, [this](const boost::system::error_code& ec, std::size_t len) {
        if (ec == asio::error::operation_aborted) return;
        if (!ec) {
          const std::string from =
              sender_.address().to_string() + ":" + std::to_string(sender_.port());
          processDatagram(buffer_.data(), len, from);
        }
        armReceive();
      });
}

bool DisInputSource::isStale(std::uint64_t packed, std::uint32_t ts) {
  const auto it = last_ts_.find(packed);
  if (it == last_ts_.end()) {  // first PDU for this entity — accept (receipt order)
    last_ts_.emplace(packed, ts);
    return false;
  }
  if (ts == 0) {  // absolute/relative-agnostic zero timestamp — accept
    it->second = ts;
    return false;
  }
  const std::uint32_t last = it->second;
  if (ts >= last) {  // newer or equal — accept
    it->second = ts;
    return false;
  }
  // ts < last: hourly wraparound if the gap spans more than half the 31-bit
  // range, otherwise genuinely stale.
  if (last - ts >= (std::uint32_t{1} << 30)) {
    it->second = ts;  // wraparound — accept
    return false;
  }
  return true;  // stale
}

void DisInputSource::processDatagram(const std::uint8_t* data, std::size_t len,
                                     const std::string& from) {
  store_.countReceived(len);

  if (len < kDisHeaderSize) {
    store_.countDropped(DropKind::kMalformed);
    log_.warn("dis", "dropped too_short len=" + std::to_string(len) + " from=" + from);
    return;
  }

  const std::uint8_t version = data[0];
  const std::uint8_t exercise = data[1];
  const std::uint8_t pdu_type = data[2];
  const std::uint32_t timestamp = beU32(data + 4);

  if (version < kMinProtocolVersion || version > kMaxProtocolVersion) {
    store_.countDropped(DropKind::kMalformed);
    log_.warn("dis", "dropped bad_version version=" + std::to_string(version) + " from=" + from);
    return;
  }

  // Exercise filter and unsupported PDU kinds are valid DIS traffic the backend
  // simply does not consume: logged at kDebug, counted in udp_received only
  // (§3.2, §3.5) — never kMalformed, never kStale.
  if (cfg_.exercise_id && exercise != *cfg_.exercise_id) {
    log_.debug("dis", "filtered exercise=" + std::to_string(exercise) + " from=" + from);
    return;
  }
  if (pdu_type != kEntityStatePduType) {
    log_.debug("dis", "ignored pdu kind=" + std::to_string(pdu_type) + " from=" + from);
    return;
  }

  // Decode the Entity State PDU. open-dis's DataStream reads via std::vector::at,
  // so a datagram too short for the body (or a bogus articulation count) throws
  // std::out_of_range; treat any decode failure as malformed, mirroring the
  // OLV1 kMalformed path.
  DIS::EntityStatePdu pdu;
  try {
    DIS::DataStream ds(reinterpret_cast<const char*>(data), len, DIS::BIG);
    pdu.unmarshal(ds);
  } catch (const std::exception&) {
    store_.countDropped(DropKind::kMalformed);
    log_.warn("dis", "dropped malformed_body len=" + std::to_string(len) + " from=" + from);
    return;
  }

  const DIS::EntityID& eid = pdu.getEntityID();
  const std::uint16_t site = eid.getSite();
  const std::uint16_t app = eid.getApplication();
  const std::uint16_t entity = eid.getEntity();
  const std::uint64_t packed = pack48(site, app, entity);
  const std::string idstr = entityIdString(site, app, entity);

  // Per-entity DIS-timestamp staleness (§3.6). The 31 MSBs are time-of-hour
  // ticks; the LSB is the absolute/relative flag, dropped for ordering.
  const std::uint32_t ts = timestamp >> 1;
  if (isStale(packed, ts)) {
    store_.countDropped(DropKind::kStale);
    log_.debug("dis", "dropped stale ts=" + std::to_string(ts) + " entity=" + idstr);
    return;
  }

  const DIS::Vector3Double& loc = pdu.getEntityLocation();
  const DIS::Vector3Float& vel = pdu.getEntityLinearVelocity();
  const bool is_satellite = (site == sat_site_ && app == sat_app_ && entity == sat_entity_);

  proto::StatePacket pkt;
  pkt.sequence = ++seq_;  // synthesized, strictly increasing (§3.6)

  std::uint8_t obj_type = 0;
  if (is_satellite) {
    sat_.id = foldEntityId(site, app, entity);
    sat_.seq = pkt.sequence;
    sat_.px = loc.getX();
    sat_.py = loc.getY();
    sat_.pz = loc.getZ();
    sat_.vx = vel.getX();
    sat_.vy = vel.getY();
    sat_.vz = vel.getZ();
  } else {
    const std::uint32_t folded = foldEntityId(site, app, entity);
    // Fold-collision check: a folded id already claimed by a *different* full
    // EntityID (§3.3). Log one kWarn per colliding pair; let the newer entity
    // overwrite — a documented limitation, not an error.
    const auto [it, inserted] = object_fold_.emplace(folded, packed);
    if (!inserted && it->second != packed) {
      std::uint16_t ps = 0, pa = 0, pe = 0;
      unpack48(it->second, ps, pa, pe);
      log_.warn("dis", "fold collision folded=" + std::to_string(folded) + " entity=" + idstr +
                           " prev=" + entityIdString(ps, pa, pe));
      it->second = packed;
    }
    const DIS::EntityType& et = pdu.getEntityType();
    obj_type = mapObjectType(et.getEntityKind(), et.getDomain());
    proto::ObjectRecord r;
    r.id = folded;
    r.type = obj_type;
    r.flags = proto::kFlagHasVelocity;  // Entity State PDUs always carry velocity
    r.confidence = 100;
    r.intensity = 0.0f;
    r.px = loc.getX();
    r.py = loc.getY();
    r.pz = loc.getZ();
    r.vx = vel.getX();
    r.vy = vel.getY();
    r.vz = vel.getZ();
    pkt.objects.push_back(r);
  }

  // Carry the last-known satellite into every packet, since apply replaces the
  // satellite wholesale on every accepted packet (§3.6). Before the satellite
  // entity is first seen these stay zeroed, as with an OLV1 stream whose sender
  // has not populated the satellite record.
  pkt.sat_id = sat_.id;
  pkt.sat_px = sat_.px;
  pkt.sat_py = sat_.py;
  pkt.sat_pz = sat_.pz;
  pkt.sat_vx = sat_.vx;
  pkt.sat_vy = sat_.vy;
  pkt.sat_vz = sat_.vz;
  pkt.object_total = static_cast<std::uint16_t>(
      std::min<std::size_t>(object_fold_.size(), proto::kMaxTrackedObjects));

  store_.apply(pkt, std::chrono::system_clock::now(), std::chrono::steady_clock::now());
  log_.debug("dis", "accepted seq=" + std::to_string(pkt.sequence) + " entity=" + idstr +
                        (is_satellite ? std::string(" satellite")
                                      : " type=" + std::string(proto::toString(
                                                       static_cast<proto::ObjectType>(obj_type)))) +
                        " total=" + std::to_string(pkt.object_total) + " from=" + from);
}

}  // namespace olv
