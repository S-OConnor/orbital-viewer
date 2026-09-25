// generator.hpp — deterministic synthetic scenario generator for load-
// testing olv_sim without a CSV file (`olv_sim --generate N`).
//
// Everything about the scenario (object count, category mix, per-object
// orbital/positional parameters, IDs) is fixed at construction time from
// `seed` using a small in-house LCG (never std::random_device / time-based
// seeding, so a given (count, seed) always reproduces byte-identical
// frames). Only `frameAt(t)` varies with simulated time t (seconds).
//
// Physics/model summary (see generator.cpp for the exact formulas):
//  - Primary satellite: circular orbit, altitude 550 km, inclination 53 deg.
//    Position/velocity come from the standard circular-orbit parametrization
//    (position on a circle in the orbital plane, rotated into ECEF by
//    inclination; velocity is the exact time-derivative of that position —
//    see computeOrbit() in the .cpp). Earth's rotation is not applied to the
//    orbital plane itself, matching the "documented simplification" already
//    used for STAR fixed-ECEF positions in docs/PROTOCOL_UDP.md.
//  - Objects (~N total, mix chosen per-object by a random draw so exact
//    counts vary slightly for small N — see scripts/make_example_csv.py for
//    an exact-count variant used by the committed example CSV):
//      ~70% DEBRIS:    circular LEO shells, altitude 400-2000 km, random
//                      inclination/RAAN/phase (same orbit math as above).
//      ~10% SATELLITE: same family as DEBRIS, tagged as another satellite.
//      ~5%  COMET:     fixed direction at radius 5e10-2e11 m with a slow,
//                      bounded sinusoidal radial drift (stays finite forever
//                      so --duration 0 runs never exceed the 1e13 m bound).
//      ~5%  STAR:      fixed direction * 1e12 m, no velocity, intensity =
//                      magnitude 1-6.
//      ~10% GROUND_HOT: fixed lat/lon on a r=6371000 m sphere, stationary in
//                      ECEF, no velocity, intensity 400-1800 K, confidence
//                      60-100.
//  - IDs are assigned 1..N at construction and never change.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "frame_builder.hpp"

namespace olv::sim {

class Generator {
 public:
  Generator(std::size_t object_count, std::uint32_t seed);

  // Builds the frame for simulated time t (seconds since the run started).
  // Pure function of (t, construction parameters); safe to call repeatedly
  // with increasing t.
  Frame frameAt(double t) const;

  std::size_t objectCount() const { return objects_.size(); }

 private:
  // Fixed per-object parameters drawn once at construction. Only the fields
  // relevant to `type` are meaningful; see generator.cpp::computeState().
  struct ObjectParams {
    std::uint32_t id = 0;
    std::uint8_t type = 0;  // proto::ObjectType
    std::uint8_t confidence = 100;
    float intensity = 0.0f;
    bool has_velocity = false;

    // DEBRIS / SATELLITE: circular orbit.
    double orbit_r = 0.0;
    double orbit_incl = 0.0;
    double orbit_raan = 0.0;
    double orbit_phase0 = 0.0;
    double orbit_omega = 0.0;

    // COMET: direction with a slow bounded sinusoidal radial drift.
    double comet_dir_x = 0.0, comet_dir_y = 0.0, comet_dir_z = 0.0;
    double comet_r0 = 0.0;
    double comet_amp = 0.0;
    double comet_omega = 0.0;
    double comet_phase = 0.0;

    // STAR / GROUND_HOT: fixed (time-independent) ECEF position.
    double fixed_x = 0.0, fixed_y = 0.0, fixed_z = 0.0;
  };

  // Primary satellite orbital parameters (fixed).
  std::uint32_t sat_id_ = 1;
  double sat_r_ = 0.0;
  double sat_incl_ = 0.0;
  double sat_omega_ = 0.0;

  std::vector<ObjectParams> objects_;
};

}  // namespace olv::sim
