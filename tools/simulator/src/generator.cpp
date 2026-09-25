// generator.cpp — see generator.hpp for the model summary; exact math here.

#include "generator.hpp"

#include <cmath>

#include "olv/protocol.hpp"

namespace olv::sim {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
// WGS-84-ish sphere, matching the ground-hot radius used throughout PLAN.md.
constexpr double kEarthRadiusM = 6'371'000.0;
// Standard gravitational parameter of Earth, m^3/s^2.
constexpr double kMuEarth = 3.986004418e14;

// Minimal deterministic LCG (Numerical Recipes constants: 1664525 /
// 1013904223, mod 2^32 via uint32 wraparound). Never seeded from
// std::random_device/time — reproducibility given (count, seed) is the
// entire point of --generate.
class Lcg {
 public:
  explicit Lcg(std::uint32_t seed) : state_(seed) {}

  std::uint32_t nextU32() {
    state_ = state_ * 1664525u + 1013904223u;
    return state_;
  }

  double next01() { return static_cast<double>(nextU32()) / 4294967296.0; }  // [0, 1)
  double range(double lo, double hi) { return lo + next01() * (hi - lo); }

 private:
  std::uint32_t state_;
};

struct Vec3 {
  double x = 0.0, y = 0.0, z = 0.0;
};

// Rotates an in-plane vector (x_orb, y_orb, 0) by inclination about the
// x-axis, then by RAAN about the z-axis. The same rotation is applied to
// position and velocity below (rotations here are fixed/time-independent
// and linear, so rotating the position's analytic time-derivative yields a
// velocity that is exactly the rotated position's derivative).
Vec3 rotateInclRaan(double x_orb, double y_orb, double incl, double raan) {
  const double y1 = y_orb * std::cos(incl);
  const double z1 = y_orb * std::sin(incl);
  const double x1 = x_orb;
  const double cr = std::cos(raan);
  const double sr = std::sin(raan);
  return {x1 * cr - y1 * sr, x1 * sr + y1 * cr, z1};
}

struct OrbitState {
  double px = 0.0, py = 0.0, pz = 0.0;
  double vx = 0.0, vy = 0.0, vz = 0.0;
};

// Standard circular-orbit parametrization: position on a circle of radius r
// in the orbital plane at angle (phase0 + omega*t); velocity is the exact
// analytic time-derivative, magnitude r*omega — the correct circular
// orbital speed when omega = sqrt(mu/r^3). The plane is then tilted by
// `incl` and rotated by `raan` into the ECEF-like frame. Earth's own
// rotation is not modeled (the orbital plane is fixed in this frame) — a
// documented simplification, consistent with STAR's fixed-ECEF treatment in
// docs/PROTOCOL_UDP.md.
OrbitState circularOrbitState(double r, double incl, double raan, double phase0, double omega,
                              double t) {
  const double ang = phase0 + omega * t;
  const double x_orb = r * std::cos(ang);
  const double y_orb = r * std::sin(ang);
  const double vx_orb = -r * omega * std::sin(ang);
  const double vy_orb = r * omega * std::cos(ang);

  const Vec3 pos = rotateInclRaan(x_orb, y_orb, incl, raan);
  const Vec3 vel = rotateInclRaan(vx_orb, vy_orb, incl, raan);
  return {pos.x, pos.y, pos.z, vel.x, vel.y, vel.z};
}

}  // namespace

Generator::Generator(std::size_t object_count, std::uint32_t seed) {
  sat_id_ = 1;
  sat_r_ = kEarthRadiusM + 550'000.0;  // 550 km altitude
  sat_incl_ = 53.0 * kDegToRad;
  sat_omega_ = std::sqrt(kMuEarth / (sat_r_ * sat_r_ * sat_r_));

  Lcg rng(seed);
  objects_.reserve(object_count);

  auto fillOrbitObject = [&](ObjectParams& p, proto::ObjectType type) {
    p.type = static_cast<std::uint8_t>(type);
    p.has_velocity = true;
    p.confidence = static_cast<std::uint8_t>(rng.range(50.0, 100.0));
    p.intensity = 0.0f;  // debris/satellite: "others: 0" per docs/PROTOCOL_UDP.md
    const double altitude = rng.range(400'000.0, 2'000'000.0);  // 400-2000 km shells
    p.orbit_r = kEarthRadiusM + altitude;
    p.orbit_incl = rng.range(0.0, kPi);
    p.orbit_raan = rng.range(0.0, 2.0 * kPi);
    p.orbit_phase0 = rng.range(0.0, 2.0 * kPi);
    p.orbit_omega = std::sqrt(kMuEarth / (p.orbit_r * p.orbit_r * p.orbit_r));
  };

  auto fillComet = [&](ObjectParams& p) {
    p.type = static_cast<std::uint8_t>(proto::ObjectType::kComet);
    p.has_velocity = true;
    p.confidence = static_cast<std::uint8_t>(rng.range(50.0, 100.0));
    p.intensity = 0.0f;
    const double theta = rng.range(0.0, kPi);      // polar angle
    const double phi = rng.range(0.0, 2.0 * kPi);  // azimuth
    p.comet_dir_x = std::sin(theta) * std::cos(phi);
    p.comet_dir_y = std::sin(theta) * std::sin(phi);
    p.comet_dir_z = std::cos(theta);
    p.comet_r0 = rng.range(5e10, 2e11);
    // Slow, bounded sinusoidal radial drift: keeps |pos| within
    // [r0-amp, r0+amp] forever, so `--duration 0` runs never approach the
    // 1e13 m protocol bound, while still giving a non-zero, physically
    // consistent (derivative-exact) velocity.
    p.comet_amp = rng.range(1e8, 1e9);
    const double period_s = rng.range(5.0 * 3600.0, 50.0 * 3600.0);
    p.comet_omega = 2.0 * kPi / period_s;
    p.comet_phase = rng.range(0.0, 2.0 * kPi);
  };

  auto fillStar = [&](ObjectParams& p) {
    p.type = static_cast<std::uint8_t>(proto::ObjectType::kStar);
    p.has_velocity = false;
    p.confidence = 100;
    p.intensity = static_cast<float>(rng.range(1.0, 6.0));  // apparent magnitude
    const double theta = rng.range(0.0, kPi);
    const double phi = rng.range(0.0, 2.0 * kPi);
    constexpr double kStarRadiusM = 1e12;
    p.fixed_x = kStarRadiusM * std::sin(theta) * std::cos(phi);
    p.fixed_y = kStarRadiusM * std::sin(theta) * std::sin(phi);
    p.fixed_z = kStarRadiusM * std::cos(theta);
  };

  auto fillGroundHot = [&](ObjectParams& p) {
    p.type = static_cast<std::uint8_t>(proto::ObjectType::kGroundHot);
    p.has_velocity = false;
    p.confidence = static_cast<std::uint8_t>(rng.range(60.0, 100.0));
    p.intensity = static_cast<float>(rng.range(400.0, 1800.0));  // Kelvin
    const double lat = rng.range(-90.0, 90.0) * kDegToRad;
    const double lon = rng.range(-180.0, 180.0) * kDegToRad;
    p.fixed_x = kEarthRadiusM * std::cos(lat) * std::cos(lon);
    p.fixed_y = kEarthRadiusM * std::cos(lat) * std::sin(lon);
    p.fixed_z = kEarthRadiusM * std::sin(lat);
  };

  for (std::size_t i = 0; i < object_count; ++i) {
    ObjectParams p;
    p.id = static_cast<std::uint32_t>(i + 1);
    const double roll = rng.next01();
    if (roll < 0.70) {
      fillOrbitObject(p, proto::ObjectType::kDebris);
    } else if (roll < 0.80) {
      fillOrbitObject(p, proto::ObjectType::kSatellite);
    } else if (roll < 0.85) {
      fillComet(p);
    } else if (roll < 0.90) {
      fillStar(p);
    } else {
      fillGroundHot(p);
    }
    objects_.push_back(p);
  }
}

Frame Generator::frameAt(double t) const {
  Frame frame;
  frame.t = t;

  const OrbitState sat =
      circularOrbitState(sat_r_, sat_incl_, /*raan=*/0.0, /*phase0=*/0.0, sat_omega_, t);
  frame.sat.id = sat_id_;
  frame.sat.px = sat.px;
  frame.sat.py = sat.py;
  frame.sat.pz = sat.pz;
  frame.sat.vx = static_cast<float>(sat.vx);
  frame.sat.vy = static_cast<float>(sat.vy);
  frame.sat.vz = static_cast<float>(sat.vz);

  frame.objects.reserve(objects_.size());
  for (const ObjectParams& p : objects_) {
    proto::ObjectRecord r;
    r.id = p.id;
    r.type = p.type;
    r.confidence = p.confidence;
    r.intensity = p.intensity;

    switch (static_cast<proto::ObjectType>(p.type)) {
      case proto::ObjectType::kDebris:
      case proto::ObjectType::kSatellite: {
        const OrbitState s = circularOrbitState(p.orbit_r, p.orbit_incl, p.orbit_raan,
                                                p.orbit_phase0, p.orbit_omega, t);
        r.px = s.px;
        r.py = s.py;
        r.pz = s.pz;
        r.vx = static_cast<float>(s.vx);
        r.vy = static_cast<float>(s.vy);
        r.vz = static_cast<float>(s.vz);
        break;
      }
      case proto::ObjectType::kComet: {
        const double phase = p.comet_omega * t + p.comet_phase;
        const double radius = p.comet_r0 + p.comet_amp * std::sin(phase);
        const double radial_rate = p.comet_amp * p.comet_omega * std::cos(phase);
        r.px = p.comet_dir_x * radius;
        r.py = p.comet_dir_y * radius;
        r.pz = p.comet_dir_z * radius;
        r.vx = static_cast<float>(p.comet_dir_x * radial_rate);
        r.vy = static_cast<float>(p.comet_dir_y * radial_rate);
        r.vz = static_cast<float>(p.comet_dir_z * radial_rate);
        break;
      }
      case proto::ObjectType::kStar:
      case proto::ObjectType::kGroundHot:
      default: {
        r.px = p.fixed_x;
        r.py = p.fixed_y;
        r.pz = p.fixed_z;
        r.vx = r.vy = r.vz = 0.0f;
        break;
      }
    }

    r.flags = p.has_velocity ? proto::kFlagHasVelocity : 0;
    frame.objects.push_back(r);
  }

  return frame;
}

}  // namespace olv::sim
