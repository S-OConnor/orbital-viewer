// olv2_input_source.cpp — see olv2_input_source.hpp. Thread: private
// io_context on an internal std::thread running the async receive loop; each
// datagram is decoded via proto::olv2::decode, checked for per-track
// staleness (§4 item 11), translated into a StateStore TrackUpdate, and
// applied.

#include "olv/olv2_input_source.hpp"

#include <cstdio>
#include <string>

#include "olv/logger.hpp"

namespace olv {

namespace asio = boost::asio;
using asio::ip::udp;

namespace {

// Fixed 3-decimal formatting for UTC epoch seconds in log lines, matching the
// WS protocol's `%.3f` (docs/PROTOCOL_WS.md §2).
std::string formatTime(double t) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.3f", t);
  return buf;
}

}  // namespace

Olv2InputSource::Olv2InputSource(const Olv2InputConfig& cfg, StateStore& store, Logger& log)
    : socket_(ioc_, udp::endpoint(asio::ip::make_address(cfg.bind_address), cfg.port)),
      store_(store),
      log_(log) {}

Olv2InputSource::~Olv2InputSource() {
  stop();
}

void Olv2InputSource::start() {
  if (started_) return;
  started_ = true;
  armReceive();
  thread_ = std::thread([this] { ioc_.run(); });
}

void Olv2InputSource::stop() {
  if (!started_) return;
  asio::post(ioc_, [this] {
    boost::system::error_code ec;
    socket_.close(ec);
    ioc_.stop();
  });
  if (thread_.joinable()) thread_.join();
  started_ = false;
}

void Olv2InputSource::armReceive() {
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

void Olv2InputSource::processDatagram(const std::uint8_t* data, std::size_t len,
                                      const std::string& from) {
  store_.countReceived(len);

  proto::olv2::TrackPacket pkt;
  const proto::olv2::DecodeError err = proto::olv2::decode(data, len, pkt);
  if (err != proto::olv2::DecodeError::kNone) {
    store_.countDropped(DropKind::kMalformed);
    log_.warn("olv2", std::string("dropped ") + proto::olv2::toString(err) +
                          " len=" + std::to_string(len) + " from=" + from);
    return;
  }

  const proto::olv2::Point& newest = pkt.points.back();

  // Per-track staleness (§4 item 11): the first datagram for a track is
  // always accepted.
  const auto last_it = last_t_.find(pkt.track_id);
  if (last_it != last_t_.end() && pkt.points.front().t <= last_it->second) {
    store_.countDropped(DropKind::kStale);
    log_.debug("olv2", "dropped stale track=" + std::to_string(pkt.track_id) +
                           " t=" + formatTime(pkt.points.front().t) + " from=" + from);
    return;
  }

  TrackUpdate u;

  // Target: the newest point, mapped onto the shared object-row shape.
  u.target.id = pkt.track_id;
  u.target.type = pkt.target_type;
  u.target.confidence = pkt.confidence;
  u.target.flags = static_cast<std::uint8_t>(
      (pkt.target_flags & proto::kFlagHighlight) |
      (newest.tgtHasVelocity() ? proto::kFlagHasVelocity : std::uint8_t{0}));
  u.target.px = newest.tgt_px;
  u.target.py = newest.tgt_py;
  u.target.pz = newest.tgt_pz;
  if (newest.tgtHasVelocity()) {
    u.target.vx = newest.tgt_vx;
    u.target.vy = newest.tgt_vy;
    u.target.vz = newest.tgt_vz;
  }
  u.target.intensity = 0.0f;

  // Satellite: the newest point's sample; velocity from the wire when present,
  // else finite-differenced from the last two samples (A3), else 0.
  u.satellite.id = pkt.sat_id;
  u.satellite.seq = pkt.sequence;
  u.satellite.px = newest.sat_px;
  u.satellite.py = newest.sat_py;
  u.satellite.pz = newest.sat_pz;
  if (newest.satHasVelocity()) {
    u.satellite.vx = newest.sat_vx;
    u.satellite.vy = newest.sat_vy;
    u.satellite.vz = newest.sat_vz;
  } else if (pkt.points.size() >= 2) {
    const proto::olv2::Point& prev = pkt.points[pkt.points.size() - 2];
    const double dt = newest.t - prev.t;
    u.satellite.vx = static_cast<float>((newest.sat_px - prev.sat_px) / dt);
    u.satellite.vy = static_cast<float>((newest.sat_py - prev.sat_py) / dt);
    u.satellite.vz = static_cast<float>((newest.sat_pz - prev.sat_pz) / dt);
  }
  u.satellite_t = newest.t;

  // Trail: every point of the datagram, ascending t (already the wire order).
  u.trail.reserve(pkt.points.size());
  for (const proto::olv2::Point& p : pkt.points) {
    u.trail.push_back(TrailPoint{pkt.track_id, p.t, p.tgt_px, p.tgt_py, p.tgt_pz});
  }

  if (last_sat_id_ && *last_sat_id_ != pkt.sat_id) {
    log_.warn("olv2", "satellite id changed old=" + std::to_string(*last_sat_id_) +
                          " new=" + std::to_string(pkt.sat_id));
  }
  last_sat_id_ = pkt.sat_id;

  store_.applyTrack(u, std::chrono::system_clock::now(), std::chrono::steady_clock::now());
  last_t_[pkt.track_id] = newest.t;

  log_.debug("olv2", "accepted track=" + std::to_string(pkt.track_id) +
                         " points=" + std::to_string(pkt.points.size()) +
                         " seq=" + std::to_string(pkt.sequence) + " from=" + from);
}

}  // namespace olv
