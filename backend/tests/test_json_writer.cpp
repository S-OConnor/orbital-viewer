// test_json_writer.cpp — WS JSON shape and numeric formatting (substring
// assertions; float formatting checked against snprintf, not hardcoded ties).

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

#include "json_writer.hpp"
#include "olv/protocol.hpp"
#include "olv_test.hpp"
#include "state_store.hpp"

using namespace olv;

namespace {

bool contains(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}

std::string fmt(double v, const char* spec) {
  char buf[48];
  std::snprintf(buf, sizeof(buf), spec, v);
  return std::string(buf);
}

}  // namespace

OLV_TEST(json_empty_snapshot) {
  Snapshot snap;  // no satellite, no objects, no last_data_time
  const std::string msg = buildStateMessage(snap, std::chrono::system_clock::time_point{}, 0);
  OLV_CHECK(contains(msg, "\"type\":\"state\""));
  OLV_CHECK(contains(msg, "\"satellite\":null"));
  OLV_CHECK(contains(msg, "\"lastDataTime\":null"));
  OLV_CHECK(contains(msg, "\"objectCount\":0"));
  OLV_CHECK(contains(msg, "\"objects\":[]"));
}

OLV_TEST(json_populated_snapshot) {
  Snapshot snap;
  SatelliteState sat;
  sat.id = 1;
  sat.seq = 42;
  sat.px = 1234567.85;  // exercises 0.1 m rounding
  sat.py = -4681712.5;
  sat.pz = 1003432.1;
  sat.vx = 1234.56f;
  sat.vy = -6.70f;
  sat.vz = 0.0f;
  snap.satellite = sat;
  snap.last_data_time = std::chrono::system_clock::time_point{} + std::chrono::seconds{1};

  SnapshotObject o1;  // has velocity
  o1.id = 1001;
  o1.type = static_cast<std::uint8_t>(proto::ObjectType::kDebris);
  o1.flags = proto::kFlagHasVelocity;
  o1.confidence = 87;
  o1.px = 6923371.4;
  o1.py = 12000.0;
  o1.pz = -55000.2;
  o1.vx = 7611.0f;
  o1.vy = 0.0f;
  o1.vz = 0.0f;
  o1.intensity = 0.0f;
  snap.objects.push_back(o1);

  SnapshotObject o2;  // no velocity, ground-hot, highlighted
  o2.id = 2001;
  o2.type = static_cast<std::uint8_t>(proto::ObjectType::kGroundHot);
  o2.flags = proto::kFlagHighlight;
  o2.confidence = 95;
  o2.px = 1113194.9;
  o2.py = -4842330.0;
  o2.pz = 3985029.2;
  o2.intensity = 1450.0f;
  snap.objects.push_back(o2);

  const std::string msg = buildStateMessage(snap, std::chrono::system_clock::time_point{}, 3);

  // Satellite formatting: positions %.1f, velocities %.2f.
  OLV_CHECK(contains(msg, "\"satellite\":{\"id\":1,\"seq\":42"));
  OLV_CHECK(contains(msg, fmt(1234567.85, "%.1f")));  // ECEF pass-through (0.1 m)
  OLV_CHECK(contains(msg, "\"pos\":[" + fmt(1234567.85, "%.1f") + "," + fmt(-4681712.5, "%.1f") +
                              "," + fmt(1003432.1, "%.1f") + "]"));
  OLV_CHECK(contains(msg, "\"vel\":[" + fmt(1234.56, "%.2f") + "," + fmt(-6.70, "%.2f") + "," +
                              fmt(0.0, "%.2f") + "]"));

  // Row with velocity vs row with null velocity; flags/conf/intensity columns.
  OLV_CHECK(contains(msg, "[1001,1,"));
  OLV_CHECK(contains(msg, "," + fmt(7611.0, "%.2f") + ","));    // velocity present
  OLV_CHECK(contains(msg, ",87," + fmt(0.0, "%.1f") + ",1]"));  // conf,intensity,flags
  OLV_CHECK(contains(msg, "[2001,5,"));
  OLV_CHECK(contains(msg, "null,null,null"));                      // missing velocity
  OLV_CHECK(contains(msg, ",95," + fmt(1450.0, "%.1f") + ",2]"));  // conf,intensity,flags

  OLV_CHECK(contains(msg, "\"objectCount\":2"));
  OLV_CHECK(contains(msg, "\"wsClients\":3"));
  OLV_CHECK(contains(msg, "\"lastDataTime\":\""));
}

OLV_TEST(json_stats_dropped_is_malformed_plus_stale) {
  Snapshot snap;
  snap.stats.udp_received = 10;
  snap.stats.udp_accepted = 6;
  snap.stats.udp_dropped_malformed = 3;
  snap.stats.udp_dropped_stale = 1;
  snap.stats.udp_rate_hz = 2.0;
  snap.stats.broadcast_seq = 7;
  const std::string msg = buildStateMessage(snap, std::chrono::system_clock::time_point{}, 1);
  OLV_CHECK(contains(msg, "\"udpReceived\":10"));
  OLV_CHECK(contains(msg, "\"udpAccepted\":6"));
  OLV_CHECK(contains(msg, "\"udpDropped\":4"));  // 3 + 1
  OLV_CHECK(contains(msg, "\"udpRateHz\":" + fmt(2.0, "%.2f")));
  OLV_CHECK(contains(msg, "\"broadcastSeq\":7"));
}

OLV_TEST(json_hello_message) {
  const std::string msg = buildHelloMessage(std::chrono::system_clock::time_point{}, 1.0);
  OLV_CHECK(contains(msg, "\"type\":\"hello\""));
  OLV_CHECK(contains(msg, "\"protocolVersion\":1"));
  OLV_CHECK(contains(msg, "\"broadcastHz\":" + fmt(1.0, "%.1f")));
  OLV_CHECK(contains(msg, "\"maxObjects\":5000"));
  OLV_CHECK(contains(msg, "\"serverTime\":\""));
}
