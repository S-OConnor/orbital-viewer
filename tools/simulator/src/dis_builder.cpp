// dis_builder.cpp — see dis_builder.hpp for the mapping contract.

#include "dis_builder.hpp"

#include <dis6/EntityID.h>
#include <dis6/EntityStatePdu.h>
#include <dis6/EntityType.h>
#include <dis6/Vector3Double.h>
#include <dis6/Vector3Float.h>
#include <dis6/utils/DataStream.h>
#include <dis6/utils/Endian.h>

#include <cmath>

#include "olv/protocol.hpp"

namespace olv::sim {

namespace {

constexpr double kTicksPerSecond = 2147483648.0 / 3600.0;  // 2^31 time-of-hour ticks

// Inverse of the backend's frozen (kind, domain) -> ObjectType table (§3.4).
// STAR and any unknown type intentionally emit (0, 0), the backend's kDebris
// fallback bucket.
void mapEntityType(std::uint8_t olv_type, std::uint8_t& kind, std::uint8_t& domain) {
  using proto::ObjectType;
  switch (static_cast<ObjectType>(olv_type)) {
    case ObjectType::kSatellite:
      kind = 1;
      domain = 5;
      return;
    case ObjectType::kGroundHot:
      kind = 1;
      domain = 1;
      return;
    case ObjectType::kComet:
      kind = 2;
      domain = 0;
      return;
    default:
      kind = 0;
      domain = 0;
      return;
  }
}

std::vector<std::uint8_t> marshalOne(std::uint16_t site, std::uint16_t app, std::uint16_t entity,
                                     std::uint8_t kind, std::uint8_t domain,
                                     const olv::proto::ObjectRecord& rec, std::uint8_t exercise_id,
                                     std::uint32_t timestamp) {
  DIS::EntityStatePdu pdu;  // ctor chain sets protocolVersion=6, family=1, pduType=1
  pdu.setExerciseID(exercise_id);
  pdu.setTimestamp(timestamp);

  DIS::EntityID eid;
  eid.setSite(site);
  eid.setApplication(app);
  eid.setEntity(entity);
  pdu.setEntityID(eid);

  DIS::EntityType et;
  et.setEntityKind(kind);
  et.setDomain(domain);
  pdu.setEntityType(et);

  DIS::Vector3Double loc;
  loc.setX(rec.px);
  loc.setY(rec.py);
  loc.setZ(rec.pz);
  pdu.setEntityLocation(loc);

  DIS::Vector3Float vel;
  vel.setX(rec.vx);
  vel.setY(rec.vy);
  vel.setZ(rec.vz);
  pdu.setEntityLinearVelocity(vel);

  pdu.setLength(static_cast<unsigned short>(pdu.getMarshalledSize()));

  DIS::DataStream ds(DIS::BIG);
  pdu.marshal(ds);
  std::vector<std::uint8_t> out(ds.size());
  for (std::size_t i = 0; i < ds.size(); ++i) {
    out[i] = static_cast<std::uint8_t>(ds[static_cast<unsigned int>(i)]);
  }
  return out;
}

}  // namespace

std::vector<std::vector<std::uint8_t>> buildDisPdus(const Frame& frame, const DisEmitConfig& cfg,
                                                    double t_seconds) {
  // DIS timestamp: 31 MSBs are ticks into the current hour, LSB is the
  // absolute/relative flag (0 = relative here). fmod keeps the hourly wrap the
  // backend's staleness rule expects (§3.6).
  const double hour_s = std::fmod(t_seconds < 0.0 ? 0.0 : t_seconds, 3600.0);
  const std::uint32_t ticks = static_cast<std::uint32_t>(hour_s * kTicksPerSecond) & 0x7FFFFFFFu;
  const std::uint32_t timestamp = ticks << 1;

  std::vector<std::vector<std::uint8_t>> pdus;
  pdus.reserve(1 + frame.objects.size());

  // Satellite first, under the configured satellite EntityID; its EntityType
  // is stamped (1, 5) though the backend decides satellite-ness solely by the
  // EntityID match (§3.3/§3.4).
  pdus.push_back(marshalOne(cfg.sat_site, cfg.sat_application, cfg.sat_entity, 1, 5, frame.sat,
                            cfg.exercise_id, timestamp));

  for (const olv::proto::ObjectRecord& rec : frame.objects) {
    std::uint8_t kind = 0, domain = 0;
    mapEntityType(rec.type, kind, domain);
    pdus.push_back(marshalOne(cfg.site, static_cast<std::uint16_t>(rec.id >> 16),
                              static_cast<std::uint16_t>(rec.id & 0xFFFFu), kind, domain, rec,
                              cfg.exercise_id, timestamp));
  }
  return pdus;
}

}  // namespace olv::sim
