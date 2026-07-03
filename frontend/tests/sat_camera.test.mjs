// Unit tests for sat_camera.js (satellite POV view matrix + equidistant
// fisheye projection). Run: node --test frontend/tests/sat_camera.test.mjs
import test from 'node:test';
import assert from 'node:assert/strict';
import {
  SAT_FOV_DEG,
  SAT_NEAR,
  SAT_FAR,
  satViewMatrix,
  fisheyeProjectNdc,
} from '../js/sat_camera.js';
import { identity, transform } from '../js/math3.js';

const DEG = Math.PI / 180;
const EPS = 1e-9;

/** [m1, m5, m9] — the row of a column-major view matrix that produces the
 * view-space Y coordinate. By lookAt's construction this row IS the
 * orthonormalized world-space up vector used to build the camera frame. */
const upRow = (m) => [m[1], m[5], m[9]];

const closeVec = (a, b, eps, msg) => {
  for (let i = 0; i < a.length; i++) {
    assert.ok(Math.abs(a[i] - b[i]) < eps, `${msg}: index ${i}, got ${a[i]}, want ${b[i]}`);
  }
};

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

test('exported constants match the frozen spec', () => {
  assert.equal(SAT_FOV_DEG, 170);
  assert.equal(SAT_NEAR, 0.05);
  assert.equal(SAT_FAR, 1000);
});

// ---------------------------------------------------------------------------
// satViewMatrix
// ---------------------------------------------------------------------------

test('view matrix transforms the eye to the view-space origin', () => {
  const pos = [10, 2, 3];
  const vel = [0, 0, 1]; // arbitrary, non-degenerate for this pos
  const m = satViewMatrix(pos, vel);
  const v = transform(m, pos);
  closeVec(v.slice(0, 3), [0, 0, 0], 1e-6, 'eye -> origin');
});

test('view matrix transforms the Earth centre to [0,0,-|pos|] (nadir on -Z)', () => {
  const pos = [10, 2, 3];
  const rho = Math.hypot(...pos); // = sqrt(113) ~= 10.63014581
  const m = satViewMatrix(pos, [0, 0, 1]);
  const v = transform(m, [0, 0, 0]);
  closeVec(v, [0, 0, -rho, 1], 1e-6, 'earth centre -> [0,0,-|pos|]');
});

test('rotation part of the view matrix is orthonormal', () => {
  const m = satViewMatrix([10, 2, 3], [1, 1, 1]);
  const x = [m[0], m[1], m[2]];
  const y = [m[4], m[5], m[6]];
  const z = [m[8], m[9], m[10]];
  for (const col of [x, y, z]) {
    assert.ok(Math.abs(Math.hypot(...col) - 1) < 1e-6, `column length ~1, got ${Math.hypot(...col)}`);
  }
  const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  assert.ok(Math.abs(dot(x, y)) < 1e-6, 'x.y ~0');
  assert.ok(Math.abs(dot(y, z)) < 1e-6, 'y.z ~0');
  assert.ok(Math.abs(dot(x, z)) < 1e-6, 'x.z ~0');
});

test('up = velocity projected perpendicular to nadir: pos=[10,0,0], vel=[0,7,0] -> world +Y', () => {
  // f = normalize(-pos) = [-1,0,0]; vel.f = 0 so vel is already perpendicular
  // to the boresight -> upC = vel = [0,7,0] -> up = [0,1,0] exactly.
  const m = satViewMatrix([10, 0, 0], [0, 7, 0]);
  closeVec(upRow(m), [0, 1, 0], 1e-9, 'up row');
});

test('up-vector fallback 1 (north): vel null, vel zero, and vel parallel to pos', () => {
  // pos=[10,0,0] -> f=[-1,0,0]. Fallback 1: upC = [0,0,1] - ([0,0,1].f)f
  //   = [0,0,1] - 0*f = [0,0,1] -> up = [0,0,1] (north) exactly.
  for (const vel of [null, undefined, [0, 0, 0], [3, 0, 0]]) {
    const m = satViewMatrix([10, 0, 0], vel);
    closeVec(upRow(m), [0, 0, 1], 1e-9, `fallback-1 up row for vel=${JSON.stringify(vel)}`);
  }
});

test('up-vector fallback 2 (+X): satellite on the polar axis with vel parallel to pos', () => {
  // pos=[0,0,10] -> f=normalize(-pos)=[0,0,-1].
  //   Fallback 1: upC = [0,0,1] - ([0,0,1].f)f = [0,0,1] - (-1)*[0,0,-1] = [0,0,0] (|upC|<1e-6).
  //   Fallback 2: upC = [1,0,0] - ([1,0,0].f)f = [1,0,0] - 0 = [1,0,0] -> up = [1,0,0].
  for (const pos of [[0, 0, 10], [0, 0, -10]]) {
    for (const vel of [null, [0, 0, 5]]) {
      const m = satViewMatrix(pos, vel);
      closeVec(upRow(m), [1, 0, 0], 1e-9, `fallback-2 up row for pos=${pos}, vel=${vel}`);
    }
  }
});

test('null when |posUnits| < 1e-9', () => {
  assert.equal(satViewMatrix([0, 0, 0], [1, 0, 0]), null);
  assert.equal(satViewMatrix([1e-10, 0, 0], null), null);
  assert.equal(satViewMatrix([0, 0, 0], null), null);
});

// ---------------------------------------------------------------------------
// fisheyeProjectNdc
//
// Most cases below feed the identity() matrix as `viewMatrix` so that
// worldPoint IS the view-space point directly (transform(identity, v) = v),
// isolating the projection math from view-matrix construction (already
// covered above). A couple of tests compose satViewMatrix() + this function
// end-to-end for realism.
// ---------------------------------------------------------------------------

test('point on the boresight -> x=0, y=0, theta~0', () => {
  const r = fisheyeProjectNdc(identity(), [0, 0, -5], 1);
  assert.notEqual(r, null);
  assert.equal(r.x, 0);
  assert.equal(r.y, 0);
  assert.ok(Math.abs(r.theta) < EPS, `theta ~0, got ${r.theta}`);
});

test('theta == thetaMax (85 deg default) at aspect 1 -> radius == 1', () => {
  // Unit vector at 85 deg off boresight: [sin85, 0, -cos85].
  const v = [Math.sin(85 * DEG), 0, -Math.cos(85 * DEG)];
  const r = fisheyeProjectNdc(identity(), v, 1);
  assert.notEqual(r, null);
  assert.ok(Math.abs(r.theta - 85 * DEG) < 1e-9, `theta ~85deg, got ${r.theta}`);
  const radius = Math.hypot(r.x, r.y);
  assert.ok(Math.abs(radius - 1) < 1e-6, `radius ~1, got ${radius}`);
});

test('radius is linear in theta (30 deg vs 60 deg, both < thetaMax)', () => {
  // radius = theta / thetaMax always (independent of azimuth), so doubling
  // theta must double the radius, and each radius must equal theta/thetaMax
  // with thetaMax = 85 deg (SAT_FOV_DEG default).
  const thetaMaxRad = 85 * DEG;
  const v30 = [Math.sin(30 * DEG), 0, -Math.cos(30 * DEG)];
  const v60 = [Math.sin(60 * DEG), 0, -Math.cos(60 * DEG)];
  const r30 = fisheyeProjectNdc(identity(), v30, 1);
  const r60 = fisheyeProjectNdc(identity(), v60, 1);
  const rad30 = Math.hypot(r30.x, r30.y);
  const rad60 = Math.hypot(r60.x, r60.y);
  assert.ok(Math.abs(rad30 - (30 * DEG) / thetaMaxRad) < 1e-6, `rad30, got ${rad30}`);
  assert.ok(Math.abs(rad60 - (60 * DEG) / thetaMaxRad) < 1e-6, `rad60, got ${rad60}`);
  assert.ok(Math.abs(rad60 / rad30 - 2) < 1e-6, `doubling theta doubles radius, ratio ${rad60 / rad30}`);
});

test('aspect scaling: aspect=2 halves x only (y unchanged)', () => {
  // theta=40deg, azimuth phi=30deg off-axis point so both x and y are nonzero.
  const theta = 40 * DEG;
  const phi = 30 * DEG;
  const v = [Math.sin(theta) * Math.cos(phi), Math.sin(theta) * Math.sin(phi), -Math.cos(theta)];
  const r1 = fisheyeProjectNdc(identity(), v, 1);
  const r2 = fisheyeProjectNdc(identity(), v, 2);
  assert.ok(Math.abs(r2.x - r1.x / 2) < 1e-9, `x halved, got r1.x=${r1.x}, r2.x=${r2.x}`);
  assert.ok(Math.abs(r2.y - r1.y) < 1e-9, `y unchanged, got r1.y=${r1.y}, r2.y=${r2.y}`);
});

test('depth is monotonic in rho and clamped at SAT_NEAR/SAT_FAR', () => {
  // rho <= SAT_NEAR (0.05) clamps to depth=-1; rho >= SAT_FAR (1000) clamps
  // to depth=1; depth((500-0.05)/999.95*2-1) ~ -0.00005 at the midpoint.
  const at = (rho) => fisheyeProjectNdc(identity(), [0, 0, -rho], 1).depth;
  assert.ok(Math.abs(at(0.01) - -1) < 1e-9, 'below SAT_NEAR clamps to -1');
  assert.ok(Math.abs(at(SAT_NEAR) - -1) < 1e-9, 'at SAT_NEAR depth = -1');
  assert.ok(Math.abs(at(SAT_FAR) - 1) < 1e-9, 'at SAT_FAR depth = 1');
  assert.ok(Math.abs(at(2000) - 1) < 1e-9, 'above SAT_FAR clamps to 1');
  const samples = [0.05, 1, 10, 100, 500, 999, 1000].map(at);
  for (let i = 1; i < samples.length; i++) {
    assert.ok(samples[i] > samples[i - 1], `strictly increasing at index ${i}: ${samples[i - 1]} -> ${samples[i]}`);
  }
});

test('point at the eye -> null (rho < 1e-9)', () => {
  // Tested in isolation with an exact view-space zero vector: view matrices
  // are stored as Float32Array (see math3.js), so round-tripping a real
  // satViewMatrix() + transform() for an eye point leaves ~1e-7 residual —
  // well above this 1e-9 epsilon by design (single-precision noise floor),
  // so that composed case is not a meaningful test of this exact branch.
  assert.equal(fisheyeProjectNdc(identity(), [0, 0, 0], 1), null);
});

test('point exactly behind the camera -> null', () => {
  // dir = [0,0,1] (theta=pi), pLen = 0 < 1e-6, theta > pi/2 -> unprojectable.
  const r = fisheyeProjectNdc(identity(), [0, 0, 5], 1);
  assert.equal(r, null);
});

test('point behind but off-axis -> finite result with radius > 1 (documented fisheye behaviour)', () => {
  // Unit vector at theta=170deg, phi=0: [sin170, 0, -cos170]. This is
  // "behind" (view-space z > 0) but pLen = sin(170deg) ~ 0.1736 > 1e-6, so
  // it projects normally with r = theta/thetaMax = 170/85 = 2.
  const v = [Math.sin(170 * DEG), 0, -Math.cos(170 * DEG)];
  const r = fisheyeProjectNdc(identity(), v, 1);
  assert.notEqual(r, null);
  assert.ok(Math.abs(r.theta - 170 * DEG) < 1e-9, `theta ~170deg, got ${r.theta}`);
  const radius = Math.hypot(r.x, r.y);
  assert.ok(Math.abs(radius - 2) < 1e-6, `radius ~2, got ${radius}`);
  assert.ok(radius > 1, 'radius > 1 for a behind-but-off-axis point');
});

test('fovDeg override is respected', () => {
  // theta=45deg. With fovDeg=90 (thetaMax=45deg), radius = 45/45 = 1.
  // With the default fovDeg=170 (thetaMax=85deg), radius = 45/85 ~ 0.5294.
  const v = [Math.sin(45 * DEG), 0, -Math.cos(45 * DEG)];
  const rDefault = fisheyeProjectNdc(identity(), v, 1);
  const rOverride = fisheyeProjectNdc(identity(), v, 1, 90);
  const radiusDefault = Math.hypot(rDefault.x, rDefault.y);
  const radiusOverride = Math.hypot(rOverride.x, rOverride.y);
  assert.ok(Math.abs(radiusOverride - 1) < 1e-6, `override radius ~1, got ${radiusOverride}`);
  assert.ok(Math.abs(radiusDefault - 45 / 85) < 1e-6, `default radius ~45/85, got ${radiusDefault}`);
});
