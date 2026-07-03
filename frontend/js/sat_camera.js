// sat_camera.js — first-person satellite camera: view matrix + equidistant
// fisheye projection for the sat-view mode (see docs/FEATURE_SATVIEW.md §3-4).
//
// Conventions: scene units (1 unit = 1,000 km), ECEF axes, +Z north — same
// as renderer.js / math3.js. Boresight is fixed at nadir (Earth centre);
// screen-up follows the satellite's orbital velocity direction, projected
// perpendicular to the boresight, with a deterministic two-stage fallback
// (north, then +X) when velocity is unusable (null/zero/parallel to nadir,
// or — for the second stage — the satellite sitting on the polar axis).
//
// LOCKSTEP: fisheyeProjectNdc() below is the CPU mirror of the GLSL
// fisheyePosition() function in renderer.js (docs/FEATURE_SATVIEW.md §5.2).
// The two MUST implement identical math line-for-line, including the 1e-9 /
// 1e-6 epsilons and the boresight/behind special cases — if either changes,
// change both together.
//
// IMPORT-SAFE: pure math, no DOM / WebGL / globals at import time.

import { lookAt, normalize, dot, sub, scale, length, transform } from './math3.js';

const DEG = Math.PI / 180;

/** Vertical full field of view of the satellite view, degrees. */
export const SAT_FOV_DEG = 170;
/** Fisheye depth-range near plane, scene units. */
export const SAT_NEAR = 0.05;
/** Fisheye depth-range far plane, scene units. */
export const SAT_FAR = 1000;

/** Clamp x to [lo, hi]. */
function clamp(x, lo, hi) {
  return Math.max(lo, Math.min(hi, x));
}

/** Component of v perpendicular to the unit vector f: v - (v.f)f. */
function perp(v, f) {
  return sub(v, scale(f, dot(v, f)));
}

/**
 * View matrix for the satellite camera: eye at the satellite, boresight
 * fixed at nadir (Earth centre), screen-up along the orbital velocity
 * direction (projected perpendicular to the boresight). Falls back to the
 * north direction (then, if the satellite sits on the polar axis, to +X)
 * whenever the velocity-derived up vector is unusable.
 *
 * @param {number[]} posUnits [x,y,z] satellite position, scene units (ECEF)
 * @param {number[]|null|undefined} velUnits [x,y,z] satellite velocity (any
 *        consistent scale, only the direction is used) or null/undefined
 * @returns {Float32Array|null} column-major 4x4 view matrix, or null when
 *          |posUnits| < 1e-9 (renderer then falls back to orbit view)
 */
export function satViewMatrix(posUnits, velUnits) {
  if (!posUnits) return null;
  const eye = posUnits;
  const posLen = length(eye);
  if (!(posLen >= 1e-9)) return null; // covers |pos| < 1e-9 and NaN

  const f = normalize(scale(eye, -1)); // boresight: nadir direction

  const velValid =
    !!velUnits &&
    Number.isFinite(velUnits[0]) &&
    Number.isFinite(velUnits[1]) &&
    Number.isFinite(velUnits[2]);
  let upC = velValid ? perp(velUnits, f) : [0, 0, 0];
  if (length(upC) < 1e-6) {
    upC = perp([0, 0, 1], f); // fallback 1: north
  }
  if (length(upC) < 1e-6) {
    upC = perp([1, 0, 0], f); // fallback 2: +X (satellite on the polar axis)
  }
  const up = normalize(upC);
  return lookAt(eye, [0, 0, 0], up);
}

/**
 * Equidistant fisheye forward projection (CPU mirror of the GLSL in
 * renderer.js — see the LOCKSTEP note at the top of this file).
 *
 * @param {Float32Array} viewMatrix from satViewMatrix()
 * @param {number[]} worldPoint [x,y,z] scene units
 * @param {number} aspect viewport width / height
 * @param {number} [fovDeg] defaults to SAT_FOV_DEG
 * @returns {{x:number,y:number,depth:number,theta:number}|null} x,y in NDC
 *          (MAY exceed [-1,1]; caller clips), depth in [-1,1], theta = angle
 *          off boresight (rad) — or null when the point is unprojectable (at
 *          the eye, or exactly behind: see the epsilons below)
 */
export function fisheyeProjectNdc(viewMatrix, worldPoint, aspect, fovDeg = SAT_FOV_DEG) {
  const thetaMax = (fovDeg / 2) * DEG;

  const vp = transform(viewMatrix, worldPoint);
  const v = [vp[0], vp[1], vp[2]];

  const rho = length(v);
  if (rho < 1e-9) return null; // at the eye: unprojectable

  const dir = scale(v, 1 / rho);
  const theta = Math.acos(clamp(-dir[2], -1, 1)); // 0 = boresight ... pi = behind
  const depth = clamp((rho - SAT_NEAR) / (SAT_FAR - SAT_NEAR), 0, 1) * 2 - 1;
  const pLen = Math.hypot(dir[0], dir[1]);

  if (pLen < 1e-6) {
    if (theta > Math.PI / 2) return null; // exactly behind: unprojectable
    return { x: 0, y: 0, depth, theta }; // boresight
  }

  const r = theta / thetaMax;
  const x = (r * (dir[0] / pLen)) / aspect;
  const y = r * (dir[1] / pLen);
  return { x, y, depth, theta };
}
