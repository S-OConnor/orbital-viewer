// state_store.hpp — the single shared, mutex-protected world state.
//
// Written by the UDP thread (apply/count*), read by the WebSocket thread
// (snapshot). This is the only synchronization point between the two threads
// (see docs/PLAN.md §2).
//
// Semantics:
//  - apply() enforces the stateful validation step (sequence staleness,
//    wraparound-safe) and merges the packet's object records into a table
//    keyed by object id. The satellite state is replaced wholesale.
//  - applyTrack() is the OLV2 entry point (docs/features/FEATURE_OLV2.md
//    §5.2): it upserts one track's newest sample, replaces the satellite only
//    when the sample is newer than the stored one, and queues every target
//    sample as a trail point. It performs no staleness check (the OLV2 input
//    source owns per-track staleness) and never touches apply()'s OLV1
//    sequence state, so OLV1/DIS behavior is unchanged.
//  - snapshot() prunes objects not refreshed within the expiry window,
//    computes the trailing 5 s accepted-packet rate, drains the pending
//    trail points, and returns a copy that the caller may use without
//    holding any lock.

#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "olv/protocol.hpp"

namespace olv {

struct Stats {
  std::uint64_t udp_received = 0;           // datagrams seen (any validity)
  std::uint64_t udp_accepted = 0;           // datagrams applied
  std::uint64_t udp_dropped_malformed = 0;  // structural/CRC/finite/range errors
  std::uint64_t udp_dropped_stale = 0;      // sequence-stale
  std::uint64_t bytes_received = 0;
  double udp_rate_hz = 0.0;         // accepted rate, trailing 5 s (snapshot only)
  std::uint64_t broadcast_seq = 0;  // filled by the broadcaster, not StateStore
};

struct SatelliteState {
  std::uint32_t id = 0;
  std::uint32_t seq = 0;  // sequence of the packet that provided this state
  double px = 0.0, py = 0.0, pz = 0.0;
  float vx = 0.0f, vy = 0.0f, vz = 0.0f;
};

struct SnapshotObject {
  std::uint32_t id = 0;
  std::uint8_t type = 0;
  std::uint8_t flags = 0;
  std::uint8_t confidence = 0;
  double px = 0.0, py = 0.0, pz = 0.0;
  float vx = 0.0f, vy = 0.0f, vz = 0.0f;
  float intensity = 0.0f;

  bool hasVelocity() const { return (flags & proto::kFlagHasVelocity) != 0; }
};

// One target sample forwarded to clients as a WS `trailPoints` row
// (docs/PROTOCOL_WS.md §2). Only the OLV2 input mode produces these.
struct TrailPoint {
  std::uint32_t id = 0;
  double t = 0.0;  // sample time, UTC seconds since the Unix epoch
  double px = 0.0, py = 0.0, pz = 0.0;
};

// Store-neutral form of one accepted OLV2 datagram; Olv2InputSource
// translates the wire packet into this so StateStore never depends on the
// OLV2 wire types.
struct TrackUpdate {
  SatelliteState satellite;       // newest satellite sample; seq = datagram sequence,
                                  // velocity already estimated when not on the wire
  double satellite_t = 0.0;       // sample time of `satellite`, for newest-wins across tracks
  SnapshotObject target;          // newest target sample, as an object row
  std::vector<TrailPoint> trail;  // every target sample of the datagram, ascending t
};

struct Snapshot {
  std::optional<SatelliteState> satellite;
  std::vector<SnapshotObject> objects;
  std::optional<std::chrono::system_clock::time_point> last_data_time;
  Stats stats;
  // Trail points applied since the previous snapshot() (drained, so each
  // point is reported exactly once). Empty outside OLV2 mode.
  std::vector<TrailPoint> trail_points;
};

enum class ApplyResult { kApplied, kStaleSequence };

enum class DropKind { kMalformed, kStale };

class StateStore {
 public:
  explicit StateStore(std::chrono::seconds object_expiry = std::chrono::seconds{15});

  // Called by the UDP thread for every datagram before decode.
  void countReceived(std::size_t bytes);

  // Called by the UDP thread when decode failed (kMalformed) — kStale is
  // counted internally by apply(); this overload exists for symmetry/tests.
  void countDropped(DropKind kind);

  // Merges a successfully decoded packet. `wall` is the receive wall-clock
  // time (becomes last_data_time), `mono` a monotonic stamp used for expiry
  // and rate computation.
  ApplyResult apply(const proto::StatePacket& pkt, std::chrono::system_clock::time_point wall,
                    std::chrono::steady_clock::time_point mono);

  // OLV2: merges one accepted track datagram. Counts it as accepted (rate
  // window, last_data_time = `wall`), upserts `u.target` (refreshing its
  // expiry at `mono`), replaces the satellite iff no satellite is stored yet,
  // the stored one came from apply(), or u.satellite_t is strictly greater
  // than the stored satellite sample time, and appends `u.trail` to the
  // pending trail buffer (oldest points dropped beyond
  // kMaxPendingTrailPoints). The caller owns staleness.
  void applyTrack(const TrackUpdate& u, std::chrono::system_clock::time_point wall,
                  std::chrono::steady_clock::time_point mono);

  // Copies the current state; prunes expired objects and old rate samples,
  // and moves the pending trail buffer into Snapshot::trail_points. Callers
  // other than the single broadcast tick would steal trail points from it.
  Snapshot snapshot(std::chrono::steady_clock::time_point mono);

  // Bound on queued trail points between snapshots (5000 tracks x 25 points).
  static constexpr std::size_t kMaxPendingTrailPoints = 125000;

  std::chrono::seconds objectExpiry() const { return object_expiry_; }

 private:
  struct StoredObject {
    SnapshotObject obj;
    std::chrono::steady_clock::time_point last_update;
  };

  std::mutex mutex_;
  std::chrono::seconds object_expiry_;
  std::optional<SatelliteState> satellite_;
  std::optional<std::chrono::system_clock::time_point> last_data_time_;
  std::unordered_map<std::uint32_t, StoredObject> objects_;
  std::deque<std::chrono::steady_clock::time_point> accept_times_;  // trailing-rate window
  Stats stats_;
  bool have_seq_ = false;
  std::uint32_t last_seq_ = 0;
  // OLV2: sample time of the stored satellite; nullopt when it came from
  // apply() (or none yet), in which case applyTrack always replaces it.
  std::optional<double> satellite_t_;
  std::vector<TrailPoint> pending_trail_;
};

}  // namespace olv
