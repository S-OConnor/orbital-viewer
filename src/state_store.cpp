// state_store.cpp — see state_store.hpp.

#include "olv/state_store.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace olv {

namespace {
constexpr std::chrono::seconds kRateWindow{5};
}  // namespace

StateStore::StateStore(std::chrono::seconds object_expiry) : object_expiry_(object_expiry) {}

void StateStore::countReceived(std::size_t bytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  ++stats_.udp_received;
  stats_.bytes_received += bytes;
}

void StateStore::countDropped(DropKind kind) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (kind == DropKind::kMalformed) {
    ++stats_.udp_dropped_malformed;
  } else {
    ++stats_.udp_dropped_stale;
  }
}

ApplyResult StateStore::apply(const proto::StatePacket& pkt,
                              std::chrono::system_clock::time_point wall,
                              std::chrono::steady_clock::time_point mono) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Sequence staleness (wraparound-safe). First packet is always accepted.
  if (have_seq_ && static_cast<std::int32_t>(pkt.sequence - last_seq_) <= 0) {
    ++stats_.udp_dropped_stale;
    return ApplyResult::kStaleSequence;
  }
  have_seq_ = true;
  last_seq_ = pkt.sequence;
  ++stats_.udp_accepted;

  satellite_ = SatelliteState{pkt.sat_id, pkt.sequence, pkt.sat_px, pkt.sat_py,
                              pkt.sat_pz, pkt.sat_vx,   pkt.sat_vy, pkt.sat_vz};
  satellite_t_.reset();

  for (const proto::ObjectRecord& r : pkt.objects) {
    StoredObject& stored = objects_[r.id];
    stored.obj = SnapshotObject{r.id, r.type, r.flags, r.confidence, r.px,       r.py,
                                r.pz, r.vx,   r.vy,    r.vz,         r.intensity};
    stored.last_update = mono;
  }

  last_data_time_ = wall;
  accept_times_.push_back(mono);
  return ApplyResult::kApplied;
}

void StateStore::applyTrack(const TrackUpdate& u, std::chrono::system_clock::time_point wall,
                            std::chrono::steady_clock::time_point mono) {
  std::lock_guard<std::mutex> lock(mutex_);
  ++stats_.udp_accepted;

  // Datagrams for different tracks interleave, so an older batch must not
  // move the satellite backwards (FEATURE_OLV2.md §4).
  if (!satellite_ || !satellite_t_ || u.satellite_t > *satellite_t_) {
    satellite_ = u.satellite;
    satellite_t_ = u.satellite_t;
  }

  StoredObject& stored = objects_[u.target.id];
  stored.obj = u.target;
  stored.last_update = mono;

  pending_trail_.insert(pending_trail_.end(), u.trail.begin(), u.trail.end());
  if (pending_trail_.size() > kMaxPendingTrailPoints) {
    const auto excess = static_cast<std::ptrdiff_t>(pending_trail_.size() - kMaxPendingTrailPoints);
    pending_trail_.erase(pending_trail_.begin(), pending_trail_.begin() + excess);
  }

  last_data_time_ = wall;
  accept_times_.push_back(mono);
}

Snapshot StateStore::snapshot(std::chrono::steady_clock::time_point mono) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Prune objects not refreshed within the expiry window.
  for (auto it = objects_.begin(); it != objects_.end();) {
    if (mono - it->second.last_update > object_expiry_) {
      it = objects_.erase(it);
    } else {
      ++it;
    }
  }

  // Prune rate samples older than the trailing window and compute rate.
  while (!accept_times_.empty() && mono - accept_times_.front() > kRateWindow) {
    accept_times_.pop_front();
  }

  Snapshot snap;
  snap.satellite = satellite_;
  snap.last_data_time = last_data_time_;
  snap.stats = stats_;
  snap.stats.udp_rate_hz = static_cast<double>(accept_times_.size()) / kRateWindow.count();
  snap.trail_points = std::move(pending_trail_);
  pending_trail_.clear();  // moved-from: make the empty state explicit

  snap.objects.reserve(objects_.size());
  for (const auto& [id, stored] : objects_) snap.objects.push_back(stored.obj);
  std::sort(snap.objects.begin(), snap.objects.end(),
            [](const SnapshotObject& a, const SnapshotObject& b) { return a.id < b.id; });

  return snap;
}

}  // namespace olv
