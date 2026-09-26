// test_dis_builder.cpp — unit tests for dis_builder.hpp (Frame -> DIS Entity
// State PDUs). Decodes the emitted buffers with the same open-dis-cpp library
// the backend uses, so the encode side is checked against the exact decode
// path it must round-trip through. OLV_TEST_MAIN() lives in
// test_csv_reader.cpp; this file links into the same olv_sim_tests binary.

#include "dis_builder.hpp"

#include <dis6/EntityStatePdu.h>
#include <dis6/utils/DataStream.h>
#include <dis6/utils/Endian.h>

#include <cstdint>
#include <vector>

#include "olv/protocol.hpp"
#include "olv_test.hpp"

namespace {

using olv::proto::ObjectRecord;
using olv::proto::ObjectType;
using olv::sim::buildDisPdus;
using olv::sim::DisEmitConfig;
using olv::sim::Frame;

constexpr std::size_t kBasePduSize = 144;  // Entity State PDU, no articulations

ObjectRecord makeObject(std::uint32_t id, ObjectType type) {
  ObjectRecord r;
  r.id = id;
  r.type = static_cast<std::uint8_t>(type);
  r.px = 1.0e6;
  r.py = -2.0e6;
  r.pz = 3.0e6;
  r.vx = 10.0f;
  r.vy = -20.0f;
  r.vz = 30.0f;
  return r;
}

Frame makeFrame() {
  Frame f;
  f.t = 42.0;
  f.sat.id = 1;
  f.sat.px = 7.0e6;
  f.sat.py = 8.0e5;
  f.sat.pz = -9.0e5;
  f.sat.vx = 7.5f;
  f.sat.vy = -1.5f;
  f.sat.vz = 0.25f;
  f.objects.push_back(makeObject(0x00012345u, ObjectType::kDebris));
  return f;
}

DIS::EntityStatePdu decode(const std::vector<std::uint8_t>& buf) {
  DIS::DataStream ds(reinterpret_cast<const char*>(buf.data()), buf.size(), DIS::BIG);
  DIS::EntityStatePdu pdu;
  pdu.unmarshal(ds);
  return pdu;
}

}  // namespace

OLV_TEST(dis_builder_emits_one_pdu_per_entity) {
  Frame f = makeFrame();
  f.objects.push_back(makeObject(7, ObjectType::kComet));
  const auto pdus = buildDisPdus(f, DisEmitConfig{}, 0.0);
  OLV_CHECK_EQ(pdus.size(), std::size_t{3});  // satellite + 2 objects
  for (const auto& p : pdus) OLV_CHECK_EQ(p.size(), kBasePduSize);
}

OLV_TEST(dis_builder_header_fields) {
  DisEmitConfig cfg;
  cfg.exercise_id = 42;
  const auto pdus = buildDisPdus(makeFrame(), cfg, 0.0);
  for (const auto& p : pdus) {
    OLV_CHECK_EQ(p[0], 6);   // protocolVersion
    OLV_CHECK_EQ(p[1], 42);  // exerciseID
    OLV_CHECK_EQ(p[2], 1);   // pduType = Entity State
    OLV_CHECK_EQ(p[3], 1);   // protocolFamily = Entity Information
    // length (bytes 8..9, big-endian) matches the actual buffer size.
    OLV_CHECK_EQ((std::size_t{p[8]} << 8) | p[9], p.size());
  }
}

OLV_TEST(dis_builder_satellite_uses_configured_entity_id) {
  DisEmitConfig cfg;
  cfg.sat_site = 3;
  cfg.sat_application = 5;
  cfg.sat_entity = 9;
  const Frame f = makeFrame();
  const auto pdus = buildDisPdus(f, cfg, 0.0);
  DIS::EntityStatePdu pdu = decode(pdus[0]);
  OLV_CHECK_EQ(pdu.getEntityID().getSite(), 3);
  OLV_CHECK_EQ(pdu.getEntityID().getApplication(), 5);
  OLV_CHECK_EQ(pdu.getEntityID().getEntity(), 9);
  OLV_CHECK_EQ(pdu.getEntityType().getEntityKind(), 1);
  OLV_CHECK_EQ(pdu.getEntityType().getDomain(), 5);
  OLV_CHECK_EQ(pdu.getEntityLocation().getX(), f.sat.px);
  OLV_CHECK_EQ(pdu.getEntityLocation().getY(), f.sat.py);
  OLV_CHECK_EQ(pdu.getEntityLocation().getZ(), f.sat.pz);
  OLV_CHECK_EQ(pdu.getEntityLinearVelocity().getX(), f.sat.vx);
  OLV_CHECK_EQ(pdu.getEntityLinearVelocity().getY(), f.sat.vy);
  OLV_CHECK_EQ(pdu.getEntityLinearVelocity().getZ(), f.sat.vz);
}

OLV_TEST(dis_builder_object_entity_id_spreads_32bit_id) {
  DisEmitConfig cfg;
  cfg.site = 11;
  const auto pdus = buildDisPdus(makeFrame(), cfg, 0.0);  // object id 0x00012345
  DIS::EntityStatePdu pdu = decode(pdus[1]);
  OLV_CHECK_EQ(pdu.getEntityID().getSite(), 11);
  OLV_CHECK_EQ(pdu.getEntityID().getApplication(), 0x0001);
  OLV_CHECK_EQ(pdu.getEntityID().getEntity(), 0x2345);
}

OLV_TEST(dis_builder_entity_type_table) {
  const struct {
    ObjectType olv;
    std::uint8_t kind, domain;
  } cases[] =
      {
          {ObjectType::kSatellite, 1, 5}, {ObjectType::kGroundHot, 1, 1},
          {ObjectType::kComet, 2, 0},     {ObjectType::kDebris, 0, 5},
          {ObjectType::kStar, 0, 0},  // no §3.4 row: decodes as unknown, documented
          {ObjectType::kUnknown, 0, 0},
      };
  for (const auto& c : cases) {
    Frame f = makeFrame();
    f.objects.clear();
    f.objects.push_back(makeObject(2, c.olv));
    const auto pdus = buildDisPdus(f, DisEmitConfig{}, 0.0);
    DIS::EntityStatePdu pdu = decode(pdus[1]);
    OLV_CHECK_EQ(pdu.getEntityType().getEntityKind(), c.kind);
    OLV_CHECK_EQ(pdu.getEntityType().getDomain(), c.domain);
  }
}

OLV_TEST(dis_builder_timestamp_monotone_with_wrap) {
  const Frame f = makeFrame();
  auto tsOf = [&](double t) {
    const auto pdus = buildDisPdus(f, DisEmitConfig{}, t);
    return (std::uint32_t{pdus[0][4]} << 24) | (std::uint32_t{pdus[0][5]} << 16) |
           (std::uint32_t{pdus[0][6]} << 8) | std::uint32_t{pdus[0][7]};
  };
  OLV_CHECK_EQ(tsOf(0.0), 0u);
  OLV_CHECK(tsOf(1.0) > tsOf(0.0));
  OLV_CHECK(tsOf(2.0) > tsOf(1.0));
  OLV_CHECK_EQ(tsOf(1.0) & 1u, 0u);       // LSB flag clear
  OLV_CHECK_EQ(tsOf(3600.0), tsOf(0.0));  // hourly wrap
  OLV_CHECK_EQ(tsOf(3601.0), tsOf(1.0));
}

OLV_TEST(dis_builder_deterministic) {
  const Frame f = makeFrame();
  OLV_CHECK(buildDisPdus(f, DisEmitConfig{}, 5.0) == buildDisPdus(f, DisEmitConfig{}, 5.0));
}
