// json_writer.cpp — see json_writer.hpp. Emits compact JSON (no spaces) so the
// integration test's substring greps match exactly.

#include "olv/json_writer.hpp"

#include <cstdio>

#include "olv/logger.hpp"
#include "olv/protocol.hpp"

namespace olv {

namespace {

// Appends `v` formatted with `fmt` (a printf float spec). Values are bounded by
// protocol validation, so %f never produces exponent notation or overflows.
void appendFloat(std::string& s, double v, const char* fmt) {
  char buf[48];
  std::snprintf(buf, sizeof(buf), fmt, v);
  s += buf;
}

void appendU64(std::string& s, std::uint64_t v) {
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(v));
  s += buf;
}

void appendInt(std::string& s, long long v) {
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%lld", v);
  s += buf;
}

}  // namespace

std::string buildStateMessage(const Snapshot& snap,
                              std::chrono::system_clock::time_point server_time, int ws_clients) {
  std::string s;
  s.reserve(snap.objects.size() * 96 + 512);

  s += "{\"type\":\"state\",\"serverTime\":\"";
  s += iso8601Utc(server_time);
  s += "\",\"lastDataTime\":";
  if (snap.last_data_time) {
    s += '"';
    s += iso8601Utc(*snap.last_data_time);
    s += '"';
  } else {
    s += "null";
  }

  s += ",\"satellite\":";
  if (snap.satellite) {
    const SatelliteState& sat = *snap.satellite;
    s += "{\"id\":";
    appendU64(s, sat.id);
    s += ",\"seq\":";
    appendU64(s, sat.seq);
    s += ",\"pos\":[";
    appendFloat(s, sat.px, "%.1f");
    s += ',';
    appendFloat(s, sat.py, "%.1f");
    s += ',';
    appendFloat(s, sat.pz, "%.1f");
    s += "],\"vel\":[";
    appendFloat(s, sat.vx, "%.2f");
    s += ',';
    appendFloat(s, sat.vy, "%.2f");
    s += ',';
    appendFloat(s, sat.vz, "%.2f");
    s += "]}";
  } else {
    s += "null";
  }

  s += ",\"objects\":[";
  for (std::size_t i = 0; i < snap.objects.size(); ++i) {
    const SnapshotObject& o = snap.objects[i];
    if (i != 0) s += ',';
    s += '[';
    appendU64(s, o.id);
    s += ',';
    appendInt(s, o.type);
    s += ',';
    appendFloat(s, o.px, "%.1f");
    s += ',';
    appendFloat(s, o.py, "%.1f");
    s += ',';
    appendFloat(s, o.pz, "%.1f");
    s += ',';
    if (o.hasVelocity()) {
      appendFloat(s, o.vx, "%.2f");
      s += ',';
      appendFloat(s, o.vy, "%.2f");
      s += ',';
      appendFloat(s, o.vz, "%.2f");
    } else {
      s += "null,null,null";
    }
    s += ',';
    appendInt(s, o.confidence);
    s += ',';
    appendFloat(s, o.intensity, "%.1f");
    s += ',';
    appendInt(s, o.flags);
    s += ']';
  }
  s += "]";

  s += ",\"stats\":{\"udpReceived\":";
  appendU64(s, snap.stats.udp_received);
  s += ",\"udpAccepted\":";
  appendU64(s, snap.stats.udp_accepted);
  s += ",\"udpDropped\":";
  appendU64(s, snap.stats.udp_dropped_malformed + snap.stats.udp_dropped_stale);
  s += ",\"udpRateHz\":";
  appendFloat(s, snap.stats.udp_rate_hz, "%.2f");
  s += ",\"wsClients\":";
  appendInt(s, ws_clients);
  s += ",\"objectCount\":";
  appendU64(s, snap.objects.size());
  s += ",\"broadcastSeq\":";
  appendU64(s, snap.stats.broadcast_seq);
  s += "}}";

  return s;
}

std::string buildHelloMessage(std::chrono::system_clock::time_point server_time,
                              double broadcast_hz) {
  std::string s;
  s.reserve(160);
  s += "{\"type\":\"hello\",\"protocolVersion\":1,\"serverTime\":\"";
  s += iso8601Utc(server_time);
  s += "\",\"broadcastHz\":";
  appendFloat(s, broadcast_hz, "%.1f");
  s += ",\"limits\":{\"maxObjects\":";
  appendU64(s, proto::kMaxTrackedObjects);
  s += "}}";
  return s;
}

}  // namespace olv
