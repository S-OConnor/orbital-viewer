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
//  - snapshot() prunes objects not refreshed within the expiry window,
//    computes the trailing 5 s accepted-packet rate, and returns a copy that
//    the caller may use without holding any lock.

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

struct Snapshot {
  std::optional<SatelliteState> satellite;
  std::vector<SnapshotObject> objects;
  std::optional<std::chrono::system_clock::time_point> last_data_time;
  Stats stats;
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

  // Copies the current state; prunes expired objects and old rate samples.
  Snapshot snapshot(std::chrono::steady_clock::time_point mono);

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
};

}  // namespace olv
