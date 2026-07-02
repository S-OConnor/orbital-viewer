// math3.js — minimal column-major mat4 + vec3 helpers for the WebGL renderer.
//
// Conventions:
//   * Matrices are 4x4, stored COLUMN-MAJOR in a Float32Array(16) — the layout
//     WebGL's gl.uniformMatrix4fv(loc, false, m) expects (transpose = false).
//     Element (row r, col c) lives at index c*4 + r.
//   * Vectors are plain length-3 (or length-4) arrays.
//   * Right-handed coordinate system; perspective/lookAt match OpenGL/WebGL.
//
// IMPORT-SAFE: this module touches no DOM / WebGL / globals at import time.

/** @returns {Float32Array} 4x4 identity matrix. */
export function identity() {
  const m = new Float32Array(16);
  m[0] = m[5] = m[10] = m[15] = 1;
  return m;
}

/**
 * Matrix product a * b (both column-major 4x4).
 * @returns {Float32Array}
 */
export function multiply(a, b) {
  const o = new Float32Array(16);
  for (let c = 0; c < 4; c++) {
    for (let r = 0; r < 4; r++) {
      o[c * 4 + r] =
        a[0 * 4 + r] * b[c * 4 + 0] +
        a[1 * 4 + r] * b[c * 4 + 1] +
        a[2 * 4 + r] * b[c * 4 + 2] +
        a[3 * 4 + r] * b[c * 4 + 3];
    }
  }
  return o;
}

/**
 * Right-handed perspective projection (OpenGL clip space, z in [-1, 1]).
 * @param {number} fovyRad vertical field of view in radians
 * @param {number} aspect  width / height
 * @param {number} near    near plane (> 0)
 * @param {number} far     far plane (> near)
 * @returns {Float32Array}
 */
export function perspective(fovyRad, aspect, near, far) {
  const f = 1 / Math.tan(fovyRad / 2);
  const nf = 1 / (near - far);
  const m = new Float32Array(16);
  m[0] = f / aspect;
  m[5] = f;
  m[10] = (far + near) * nf;
  m[11] = -1;
  m[14] = 2 * far * near * nf;
  return m;
}

/**
 * View matrix looking from `eye` toward `center` with the given up vector.
 * @returns {Float32Array}
 */
export function lookAt(eye, center, up) {
  // z = normalize(eye - center)  (camera looks down -z)
  let zx = eye[0] - center[0];
  let zy = eye[1] - center[1];
  let zz = eye[2] - center[2];
  let zl = Math.hypot(zx, zy, zz) || 1;
  zx /= zl; zy /= zl; zz /= zl;
  // x = normalize(cross(up, z))
  let xx = up[1] * zz - up[2] * zy;
  let xy = up[2] * zx - up[0] * zz;
  let xz = up[0] * zy - up[1] * zx;
  const xl = Math.hypot(xx, xy, xz) || 1;
  xx /= xl; xy /= xl; xz /= xl;
  // y = cross(z, x)
  const yx = zy * xz - zz * xy;
  const yy = zz * xx - zx * xz;
  const yz = zx * xy - zy * xx;

  const m = new Float32Array(16);
  m[0] = xx; m[1] = yx; m[2] = zx; m[3] = 0;
  m[4] = xy; m[5] = yy; m[6] = zy; m[7] = 0;
  m[8] = xz; m[9] = yz; m[10] = zz; m[11] = 0;
  m[12] = -(xx * eye[0] + xy * eye[1] + xz * eye[2]);
  m[13] = -(yx * eye[0] + yy * eye[1] + yz * eye[2]);
  m[14] = -(zx * eye[0] + zy * eye[1] + zz * eye[2]);
  m[15] = 1;
  return m;
}

/**
 * Transform a vector by a column-major matrix: returns m * v.
 * `v` may be length 3 (w assumed 1) or length 4. Always returns a length-4
 * array so callers can perform the perspective divide themselves.
 * @returns {number[]}
 */
export function transform(m, v) {
  const x = v[0], y = v[1], z = v[2];
  const w = v.length > 3 ? v[3] : 1;
  return [
    m[0] * x + m[4] * y + m[8] * z + m[12] * w,
    m[1] * x + m[5] * y + m[9] * z + m[13] * w,
    m[2] * x + m[6] * y + m[10] * z + m[14] * w,
    m[3] * x + m[7] * y + m[11] * z + m[15] * w,
  ];
}

/** Euclidean length of a vec3. */
export function length(v) {
  return Math.hypot(v[0], v[1], v[2]);
}

/** Dot product of two vec3s. */
export function dot(a, b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/** Cross product a × b (vec3). */
export function cross(a, b) {
  return [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0],
  ];
}

/** Unit vector in the direction of v (returns [0,0,0] for a zero vector). */
export function normalize(v) {
  const l = Math.hypot(v[0], v[1], v[2]);
  if (l === 0) return [0, 0, 0];
  return [v[0] / l, v[1] / l, v[2] / l];
}

/** Component-wise vec3 addition. */
export function add(a, b) {
  return [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
}

/** Component-wise vec3 subtraction (a - b). */
export function sub(a, b) {
  return [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
}

/** Scale a vec3 by a scalar. */
export function scale(v, s) {
  return [v[0] * s, v[1] * s, v[2] * s];
}
