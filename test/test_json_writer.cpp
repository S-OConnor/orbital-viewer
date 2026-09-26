// test_json_writer.cpp — WS JSON shape and numeric formatting (substring
// assertions; float formatting checked against snprintf, not hardcoded ties).

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

#include <gtest/gtest.h>

#include "olv/json_writer.hpp"
#include "olv/protocol.hpp"
#include "olv/state_store.hpp"

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

TEST(JsonWriter, json_empty_snapshot) {
  Snapshot snap;  // no satellite, no objects, no last_data_time
  const std::string msg = buildStateMessage(snap, std::chrono::system_clock::time_point{}, 0);
  EXPECT_TRUE(contains(msg, "\"type\":\"state\""));
  EXPECT_TRUE(contains(msg, "\"satellite\":null"));
  EXPECT_TRUE(contains(msg, "\"lastDataTime\":null"));
  EXPECT_TRUE(contains(msg, "\"objectCount\":0"));
  EXPECT_TRUE(contains(msg, "\"objects\":[]"));
}

TEST(JsonWriter, json_populated_snapshot) {
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
  EXPECT_TRUE(contains(msg, "\"satellite\":{\"id\":1,\"seq\":42"));
  EXPECT_TRUE(contains(msg, fmt(1234567.85, "%.1f")));  // ECEF pass-through (0.1 m)
  EXPECT_TRUE(contains(msg, "\"pos\":[" + fmt(1234567.85, "%.1f") + "," + fmt(-4681712.5, "%.1f") +
                                "," + fmt(1003432.1, "%.1f") + "]"));
  EXPECT_TRUE(contains(msg, "\"vel\":[" + fmt(1234.56, "%.2f") + "," + fmt(-6.70, "%.2f") + "," +
                                fmt(0.0, "%.2f") + "]"));

  // Row with velocity vs row with null velocity; flags/conf/intensity columns.
  EXPECT_TRUE(contains(msg, "[1001,1,"));
  EXPECT_TRUE(contains(msg, "," + fmt(7611.0, "%.2f") + ","));    // velocity present
  EXPECT_TRUE(contains(msg, ",87," + fmt(0.0, "%.1f") + ",1]"));  // conf,intensity,flags
  EXPECT_TRUE(contains(msg, "[2001,5,"));
  EXPECT_TRUE(contains(msg, "null,null,null"));                      // missing velocity
  EXPECT_TRUE(contains(msg, ",95," + fmt(1450.0, "%.1f") + ",2]"));  // conf,intensity,flags

  EXPECT_TRUE(contains(msg, "\"objectCount\":2"));
  EXPECT_TRUE(contains(msg, "\"wsClients\":3"));
  EXPECT_TRUE(contains(msg, "\"lastDataTime\":\""));
}

TEST(JsonWriter, json_stats_dropped_is_malformed_plus_stale) {
  Snapshot snap;
  snap.stats.udp_received = 10;
  snap.stats.udp_accepted = 6;
  snap.stats.udp_dropped_malformed = 3;
  snap.stats.udp_dropped_stale = 1;
  snap.stats.udp_rate_hz = 2.0;
  snap.stats.broadcast_seq = 7;
  const std::string msg = buildStateMessage(snap, std::chrono::system_clock::time_point{}, 1);
  EXPECT_TRUE(contains(msg, "\"udpReceived\":10"));
  EXPECT_TRUE(contains(msg, "\"udpAccepted\":6"));
  EXPECT_TRUE(contains(msg, "\"udpDropped\":4"));  // 3 + 1
  EXPECT_TRUE(contains(msg, "\"udpRateHz\":" + fmt(2.0, "%.2f")));
  EXPECT_TRUE(contains(msg, "\"broadcastSeq\":7"));
}

TEST(JsonWriter, json_trail_points_absent_when_empty) {
  Snapshot snap;
  SnapshotObject o;
  o.id = 1;
  snap.objects.push_back(o);  // trail_points left empty
  const std::string msg = buildStateMessage(snap, std::chrono::system_clock::time_point{}, 0);
  EXPECT_FALSE(contains(msg, "trailPoints"));
}

TEST(JsonWriter, json_trail_points_present_formatted_and_ordered) {
  Snapshot snap;
  TrailPoint a;
  a.id = 2001;
  a.t = 1790000000.125;
  a.px = 6923371.4;
  a.py = 12000.0;
  a.pz = -55000.2;
  TrailPoint b;
  b.id = 2001;
  b.t = 1790000000.225;
  b.px = 6923380.9;
  b.py = 12011.5;
  b.pz = -55001.0;
  snap.trail_points = {a, b};  // insertion order must be preserved verbatim

  const std::string msg = buildStateMessage(snap, std::chrono::system_clock::time_point{}, 0);
  const std::string expected = "\"trailPoints\":[[2001," + fmt(1790000000.125, "%.3f") + "," +
                               fmt(6923371.4, "%.1f") + "," + fmt(12000.0, "%.1f") + "," +
                               fmt(-55000.2, "%.1f") + "],[2001," + fmt(1790000000.225, "%.3f") +
                               "," + fmt(6923380.9, "%.1f") + "," + fmt(12011.5, "%.1f") + "," +
                               fmt(-55001.0, "%.1f") + "]]";
  EXPECT_TRUE(contains(msg, expected));
}

TEST(JsonWriter, json_trail_points_placed_immediately_after_objects) {
  Snapshot snap;
  TrailPoint a;
  a.id = 1;
  snap.trail_points.push_back(a);
  const std::string msg = buildStateMessage(snap, std::chrono::system_clock::time_point{}, 0);

  const auto objects_key = msg.find("\"objects\":[]");
  const auto trail_key = msg.find("\"trailPoints\":");
  const auto stats_key = msg.find("\"stats\":");
  ASSERT_NE(objects_key, std::string::npos);
  ASSERT_NE(trail_key, std::string::npos);
  ASSERT_NE(stats_key, std::string::npos);
  EXPECT_LT(objects_key, trail_key);
  EXPECT_LT(trail_key, stats_key);
  // No other key sits between the two: trailPoints starts right where the
  // empty objects array ends.
  EXPECT_EQ(trail_key, objects_key + std::string("\"objects\":[]").size() + 1);
}

TEST(JsonWriter, json_message_brackets_balanced_with_trail_points) {
  Snapshot snap;
  SnapshotObject o;
  o.id = 2;
  snap.objects.push_back(o);
  TrailPoint tp;
  tp.id = 2;
  tp.t = 5.0;
  snap.trail_points.push_back(tp);

  const std::string msg = buildStateMessage(snap, std::chrono::system_clock::time_point{}, 1);
  int braces = 0;
  int brackets = 0;
  for (char c : msg) {
    if (c == '{') ++braces;
    if (c == '}') --braces;
    if (c == '[') ++brackets;
    if (c == ']') --brackets;
  }
  EXPECT_EQ(braces, 0);
  EXPECT_EQ(brackets, 0);
  EXPECT_EQ(msg.front(), '{');
  EXPECT_EQ(msg.back(), '}');
}

TEST(JsonWriter, json_hello_message) {
  const std::string msg = buildHelloMessage(std::chrono::system_clock::time_point{}, 1.0);
  EXPECT_TRUE(contains(msg, "\"type\":\"hello\""));
  EXPECT_TRUE(contains(msg, "\"protocolVersion\":1"));
  EXPECT_TRUE(contains(msg, "\"broadcastHz\":" + fmt(1.0, "%.1f")));
  EXPECT_TRUE(contains(msg, "\"maxObjects\":5000"));
  EXPECT_TRUE(contains(msg, "\"serverTime\":\""));
}
