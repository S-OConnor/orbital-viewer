// renderer.js — custom minimal WebGL 1 renderer for the Orbital LOS Viewer.
//
// Draws: a shaded lat/lon Earth globe + 15-degree graticule, an approximate Sun
// billboard (which also lights the globe), all tracked objects as a single
// GL_POINTS draw, the primary satellite marker + 60 s velocity vector, age-faded
// GL_LINES trails, and 2D-canvas labels / selection ring on the overlay.
//
// Public API (frozen — docs/PLAN.md section 6):
//   createRenderer(glCanvas, overlayCanvas) -> {
//     resize(), setSnapshot(snap, nowMs), setSettings(s),
//     setSelected(idOrNull), frame(nowMs), pick(x, y) -> id|null }
//
// Units: 1 unit = 1,000 km (SCALE = 1e-6 m -> units). Earth radius 6.371 units,
// centred at the origin, ECEF axes with +Z toward the north pole.
//
// IMPORT-SAFE: no DOM / WebGL access happens at import time; everything runs
// inside createRenderer().

import { perspective, multiply, transform, normalize } from './math3.js';
import { createCamera } from './camera.js';
import { sunDirectionEcef } from './sun.js';

const DEG = Math.PI / 180;
const SCALE = 1e-6;      // metres -> scene units (1 unit = 1,000 km)
const EARTH_R = 6.371;   // Earth radius in units
const SHELL = 120;       // distant objects (stars) clamp onto this radius
const MAX_OBJECTS = 5000;
const TRAIL_CAP = 64;    // ring-buffer samples kept per object
const SUN_DIST = 150;    // Sun billboard distance in units

const FOV = 45 * DEG;
const NEAR = 0.5;
const FAR = 1000;

// Category appearance. groundHot colour is computed per-object from intensity.
const CAT = {
  debris:    { color: [0xaa / 255, 0xb2 / 255, 0xbd / 255], size: 3 },   // #aab2bd
  star:      { color: [1, 1, 1],                             size: 2.5 }, // #ffffff
  comet:     { color: [0x6f / 255, 0xd3 / 255, 0xff / 255], size: 5 },   // #6fd3ff
  satellite: { color: [0x58 / 255, 0xd6 / 255, 0x8d / 255], size: 5 },   // #58d68d
  groundHot: { color: [1, 0.5647, 0.251],                   size: 5 },   // base (unused directly)
};
const SAT_COLOR = [1, 0.8353, 0.2902]; // #ffd54a primary satellite
const SUN_COLOR = [1.0, 0.93, 0.7];    // warm white/yellow disc

// groundHot temperature -> colour: lerp #ff9040 (300 K) -> #ff3020 (2000 K).
const GH_LO = [1, 0.5647, 0.251];  // #ff9040
const GH_HI = [1, 0.1882, 0.1255]; // #ff3020
function groundHotColor(k) {
  let t = (k - 300) / (2000 - 300);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return [
    GH_LO[0] + (GH_HI[0] - GH_LO[0]) * t,
    GH_LO[1] + (GH_HI[1] - GH_LO[1]) * t,
    GH_LO[2] + (GH_HI[2] - GH_LO[2]) * t,
  ];
}

// ---------------------------------------------------------------------------
// Shader sources
// ---------------------------------------------------------------------------

const EARTH_VS = `
attribute vec3 aPos;
attribute vec3 aNormal;
uniform mat4 uMVP;
varying vec3 vNormal;
varying vec3 vPos;
void main() {
  vNormal = aNormal;
  vPos = aPos;
  gl_Position = uMVP * vec4(aPos, 1.0);
}`;

const EARTH_FS = `
precision mediump float;
varying vec3 vNormal;
varying vec3 vPos;
uniform vec3 uSunDir;
uniform float uAmbient;
void main() {
  vec3 N = normalize(vNormal);
  float diff = max(dot(N, normalize(uSunDir)), 0.0);
  float light = uAmbient + (1.0 - uAmbient) * diff;
  float lat = vPos.z / 6.371;                 // -1 (south) .. +1 (north)
  vec3 base = vec3(0.05, 0.19, 0.44);         // deep blue
  base += vec3(0.015, 0.03, 0.05) * cos(lat * 9.0); // subtle latitude bands
  gl_FragColor = vec4(base * light, 1.0);
}`;

const LINE_VS = `
attribute vec3 aPos;
attribute vec4 aColor;
uniform mat4 uMVP;
varying vec4 vColor;
void main() {
  vColor = aColor;
  gl_Position = uMVP * vec4(aPos, 1.0);
}`;

const LINE_FS = `
precision mediump float;
varying vec4 vColor;
void main() { gl_FragColor = vColor; }`;

const POINTS_VS = `
attribute vec3 aPos;
attribute vec3 aColor;
attribute float aSize;
uniform mat4 uMVP;
uniform float uDpr;
varying vec3 vColor;
void main() {
  vColor = aColor;
  gl_Position = uMVP * vec4(aPos, 1.0);
  gl_PointSize = aSize * uDpr;               // CSS px -> device px
}`;

const POINTS_FS = `
precision mediump float;
varying vec3 vColor;
void main() {
  vec2 c = gl_PointCoord - 0.5;
  float r = dot(c, c);
  if (r > 0.25) discard;                      // round point
  float a = smoothstep(0.25, 0.12, r);        // soft edge
  gl_FragColor = vec4(vColor, a);
}`;

// Single-point billboard used for the Sun disc and the satellite marker.
// aVertex is a dummy attribute (always fed 0) that keeps vertex attribute 0
// enabled — some GL backends require attrib 0 to be a real array for a draw.
const MARKER_VS = `
attribute float aVertex;
uniform mat4 uMVP;
uniform vec3 uWorldPos;
uniform float uSize;
void main() {
  gl_Position = uMVP * vec4(uWorldPos, 1.0);
  gl_PointSize = uSize + aVertex; // aVertex is 0, keeps the attribute live
}`;

const MARKER_FS = `
precision mediump float;
uniform vec3 uColor;
uniform float uRing;                          // 0 = soft disc (Sun), 1 = disc+ring (satellite)
void main() {
  vec2 c = gl_PointCoord - 0.5;
  float r = length(c) * 2.0;                   // 0 centre .. ~1 edge
  if (uRing > 0.5) {
    if (r < 0.42) { gl_FragColor = vec4(uColor, 1.0); }
    else if (r > 0.72 && r < 0.95) { gl_FragColor = vec4(uColor, 0.9); }
    else discard;
  } else {
    if (r > 1.0) discard;
    float a = smoothstep(1.0, 0.0, r);
    gl_FragColor = vec4(uColor, a);
  }
}`;

// ---------------------------------------------------------------------------
// Geometry builders (pure)
// ---------------------------------------------------------------------------

function buildSphere(R, stacks, slices) {
  const positions = [];
  const normals = [];
  const indices = [];
  for (let i = 0; i <= stacks; i++) {
    const phi = (i / stacks) * Math.PI - Math.PI / 2; // -pi/2 .. +pi/2 latitude
    const cp = Math.cos(phi);
    const sp = Math.sin(phi);
    for (let j = 0; j <= slices; j++) {
      const theta = (j / slices) * 2 * Math.PI;       // 0 .. 2pi longitude
      const nx = cp * Math.cos(theta);
      const ny = cp * Math.sin(theta);
      const nz = sp;                                   // +Z north pole
      positions.push(nx * R, ny * R, nz * R);
      normals.push(nx, ny, nz);
    }
  }
  const stride = slices + 1;
  for (let i = 0; i < stacks; i++) {
    for (let j = 0; j < slices; j++) {
      const a = i * stride + j;
      const b = a + 1;
      const c = a + stride;
      const d = c + 1;
      indices.push(a, c, b, b, c, d);
    }
  }
  return {
    positions: new Float32Array(positions),
    normals: new Float32Array(normals),
    indices: new Uint16Array(indices),
  };
}

// Interleaved [x,y,z, r,g,b,a] line vertices for a 15-degree graticule.
function buildGraticule(R) {
  const r = R * 1.003; // slightly raised to avoid z-fighting with the globe
  const col = [0.55, 0.72, 1.0, 0.22];
  const v = [];
  const step = 6;
  const push = (x, y, z) => v.push(x, y, z, col[0], col[1], col[2], col[3]);

  // Latitude circles at multiples of 15 deg (excluding the poles).
  for (let lat = -75; lat <= 75; lat += 15) {
    const phi = lat * DEG;
    const cp = Math.cos(phi);
    const sp = Math.sin(phi);
    let prev = null;
    for (let lon = 0; lon <= 360; lon += step) {
      const th = lon * DEG;
      const p = [r * cp * Math.cos(th), r * cp * Math.sin(th), r * sp];
      if (prev) { push(prev[0], prev[1], prev[2]); push(p[0], p[1], p[2]); }
      prev = p;
    }
  }
  // Meridians every 15 deg (pole to pole).
  for (let lon = 0; lon < 360; lon += 15) {
    const th = lon * DEG;
    const ct = Math.cos(th);
    const st = Math.sin(th);
    let prev = null;
    for (let lat = -90; lat <= 90; lat += step) {
      const phi = lat * DEG;
      const cp = Math.cos(phi);
      const p = [r * cp * ct, r * cp * st, r * Math.sin(phi)];
      if (prev) { push(prev[0], prev[1], prev[2]); push(p[0], p[1], p[2]); }
      prev = p;
    }
  }
  return new Float32Array(v);
}

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------

export function createRenderer(glCanvas, overlayCanvas) {
  const gl =
    glCanvas.getContext('webgl') || glCanvas.getContext('experimental-webgl');
  if (!gl) {
    throw new Error(
      'WebGL is not available: getContext("webgl") returned null. ' +
        'The renderer requires a WebGL 1 context.',
    );
  }
  const ctx = overlayCanvas.getContext('2d');
  if (!ctx) {
    throw new Error('Overlay 2D context is not available (getContext("2d") returned null).');
  }

  // --- GL program helpers -------------------------------------------------
  function compile(type, src) {
    const sh = gl.createShader(type);
    gl.shaderSource(sh, src);
    gl.compileShader(sh);
    if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
      const log = gl.getShaderInfoLog(sh);
      gl.deleteShader(sh);
      throw new Error('Shader compile failed: ' + log);
    }
    return sh;
  }
  function program(vsSrc, fsSrc, attribBindings) {
    const p = gl.createProgram();
    const vs = compile(gl.VERTEX_SHADER, vsSrc);
    const fs = compile(gl.FRAGMENT_SHADER, fsSrc);
    gl.attachShader(p, vs);
    gl.attachShader(p, fs);
    // Bind attribute names to fixed locations so the state-reset logic below
    // (disabling arrays 0..3) always covers every attribute we use.
    if (attribBindings) {
      for (const name in attribBindings) gl.bindAttribLocation(p, attribBindings[name], name);
    }
    gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) {
      const log = gl.getProgramInfoLog(p);
      throw new Error('Program link failed: ' + log);
    }
    gl.deleteShader(vs);
    gl.deleteShader(fs);
    return p;
  }

  const earthProg = program(EARTH_VS, EARTH_FS, { aPos: 0, aNormal: 1 });
  const lineProg = program(LINE_VS, LINE_FS, { aPos: 0, aColor: 1 });
  const pointsProg = program(POINTS_VS, POINTS_FS, { aPos: 0, aColor: 1, aSize: 2 });
  const markerProg = program(MARKER_VS, MARKER_FS, { aVertex: 0 });

  const earthU = {
    mvp: gl.getUniformLocation(earthProg, 'uMVP'),
    sun: gl.getUniformLocation(earthProg, 'uSunDir'),
    ambient: gl.getUniformLocation(earthProg, 'uAmbient'),
  };
  const lineU = { mvp: gl.getUniformLocation(lineProg, 'uMVP') };
  const pointsU = {
    mvp: gl.getUniformLocation(pointsProg, 'uMVP'),
    dpr: gl.getUniformLocation(pointsProg, 'uDpr'),
  };
  const markerU = {
    mvp: gl.getUniformLocation(markerProg, 'uMVP'),
    pos: gl.getUniformLocation(markerProg, 'uWorldPos'),
    size: gl.getUniformLocation(markerProg, 'uSize'),
    color: gl.getUniformLocation(markerProg, 'uColor'),
    ring: gl.getUniformLocation(markerProg, 'uRing'),
  };

  // --- Static geometry buffers -------------------------------------------
  const sphere = buildSphere(EARTH_R, 32, 48);
  const earthPosBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, earthPosBuf);
  gl.bufferData(gl.ARRAY_BUFFER, sphere.positions, gl.STATIC_DRAW);
  const earthNrmBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, earthNrmBuf);
  gl.bufferData(gl.ARRAY_BUFFER, sphere.normals, gl.STATIC_DRAW);
  const earthIdxBuf = gl.createBuffer();
  gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, earthIdxBuf);
  gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, sphere.indices, gl.STATIC_DRAW);
  const earthIndexCount = sphere.indices.length;

  const graticule = buildGraticule(EARTH_R);
  const gratBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, gratBuf);
  gl.bufferData(gl.ARRAY_BUFFER, graticule, gl.STATIC_DRAW);
  const gratVertCount = graticule.length / 7;

  // --- Dynamic object-point buffers (preallocated for MAX_OBJECTS) --------
  const posArr = new Float32Array(MAX_OBJECTS * 3);
  const colArr = new Float32Array(MAX_OBJECTS * 3);
  const sizeArr = new Float32Array(MAX_OBJECTS);
  const objPosBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, objPosBuf);
  gl.bufferData(gl.ARRAY_BUFFER, posArr, gl.DYNAMIC_DRAW);
  const objColBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, objColBuf);
  gl.bufferData(gl.ARRAY_BUFFER, colArr, gl.DYNAMIC_DRAW);
  const objSizeBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, objSizeBuf);
  gl.bufferData(gl.ARRAY_BUFFER, sizeArr, gl.DYNAMIC_DRAW);
  let objCount = 0;

  // --- Trail + velocity buffers -------------------------------------------
  const trailBuf = gl.createBuffer();
  let trailArr = new Float32Array(4096);
  let trailVertCount = 0;
  const velBuf = gl.createBuffer();
  let velActive = false;

  // 1-vertex buffer feeding the marker program's dummy attribute (value 0).
  const markerBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, markerBuf);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([0]), gl.STATIC_DRAW);

  // --- Renderer state -----------------------------------------------------
  let settings = normalizeSettings(null);
  let snapshot = null;
  let selectedId = null;
  let lastSnapNow = 0;
  let cssW = 1;
  let cssH = 1;
  let dpr = 1;
  let proj = perspective(FOV, 1, NEAR, FAR);

  const renderObjects = []; // {id, cat, x, y, z} for visible objects (units, clamped)
  let satRender = null;     // {id, x, y, z} or null
  const trails = new Map(); // id | 'sat:<id>' -> {cat, s:[{t,x,y,z}]}

  const camera = createCamera(overlayCanvas, {
    distance: 25,
    yaw: 0,
    pitch: 20 * DEG,
    minDistance: 8,
    maxDistance: 200,
  });

  // --- Helpers ------------------------------------------------------------
  function normalizeSettings(s) {
    s = s || {};
    const c = s.categories || {};
    let ts = typeof s.trailSeconds === 'number' ? s.trailSeconds : 20;
    ts = Math.max(0, Math.min(60, ts));
    return {
      showTrails: !!s.showTrails,
      trailSeconds: ts,
      showLabels: !!s.showLabels,
      categories: {
        debris: c.debris !== false,
        star: c.star !== false,
        comet: c.comet !== false,
        satellite: c.satellite !== false,
        groundHot: c.groundHot !== false,
      },
    };
  }

  // Metres ECEF -> scene units, clamped onto the 120-unit celestial shell for
  // very distant objects (e.g. stars given as ~1e12 m direction markers).
  function worldFromMeters(pos) {
    let x = pos[0] * SCALE;
    let y = pos[1] * SCALE;
    let z = pos[2] * SCALE;
    const len = Math.hypot(x, y, z);
    if (len > SHELL) {
      const s = SHELL / len;
      x *= s; y *= s; z *= s;
    }
    return [x, y, z];
  }

  function catVisible(cat) {
    return cat == null || settings.categories[cat] !== false;
  }

  function findRenderById(id) {
    if (satRender && satRender.id === id) return satRender;
    for (let i = 0; i < renderObjects.length; i++) {
      if (renderObjects[i].id === id) return renderObjects[i];
    }
    return null;
  }

  // --- Buffer (re)builders ------------------------------------------------
  function rebuildObjectBuffer() {
    renderObjects.length = 0;
    let n = 0;
    if (snapshot && Array.isArray(snapshot.objects)) {
      const objs = snapshot.objects;
      for (let i = 0; i < objs.length && n < MAX_OBJECTS; i++) {
        const o = objs[i];
        const def = CAT[o.cat];
        if (!def) continue;
        if (settings.categories[o.cat] === false) continue;
        const w = worldFromMeters(o.pos);
        const col = o.cat === 'groundHot' ? groundHotColor(o.intensity || 0) : def.color;
        posArr[n * 3] = w[0]; posArr[n * 3 + 1] = w[1]; posArr[n * 3 + 2] = w[2];
        colArr[n * 3] = col[0]; colArr[n * 3 + 1] = col[1]; colArr[n * 3 + 2] = col[2];
        sizeArr[n] = def.size;
        renderObjects.push({ id: o.id, cat: o.cat, x: w[0], y: w[1], z: w[2] });
        n++;
      }
    }
    objCount = n;
    gl.bindBuffer(gl.ARRAY_BUFFER, objPosBuf);
    gl.bufferSubData(gl.ARRAY_BUFFER, 0, posArr.subarray(0, n * 3));
    gl.bindBuffer(gl.ARRAY_BUFFER, objColBuf);
    gl.bufferSubData(gl.ARRAY_BUFFER, 0, colArr.subarray(0, n * 3));
    gl.bindBuffer(gl.ARRAY_BUFFER, objSizeBuf);
    gl.bufferSubData(gl.ARRAY_BUFFER, 0, sizeArr.subarray(0, n));
  }

  function rebuildSatellite() {
    satRender = null;
    velActive = false;
    const s = snapshot && snapshot.satellite;
    if (!s || !s.pos) return;
    const w = worldFromMeters(s.pos);
    satRender = { id: s.id, x: w[0], y: w[1], z: w[2] };
    if (s.vel) {
      // pos -> pos + vel * 60 s, in metres, then to units.
      const ex = (s.pos[0] + s.vel[0] * 60) * SCALE;
      const ey = (s.pos[1] + s.vel[1] * 60) * SCALE;
      const ez = (s.pos[2] + s.vel[2] * 60) * SCALE;
      const c = [1, 0.85, 0.3, 0.95];
      const arr = new Float32Array([
        w[0], w[1], w[2], c[0], c[1], c[2], c[3],
        ex, ey, ez, c[0], c[1], c[2], c[3],
      ]);
      gl.bindBuffer(gl.ARRAY_BUFFER, velBuf);
      gl.bufferData(gl.ARRAY_BUFFER, arr, gl.DYNAMIC_DRAW);
      velActive = true;
    }
  }

  function appendTrails(nowMs) {
    if (snapshot && Array.isArray(snapshot.objects)) {
      for (const o of snapshot.objects) {
        if (!CAT[o.cat]) continue;
        const w = worldFromMeters(o.pos);
        let t = trails.get(o.id);
        if (!t) { t = { cat: o.cat, s: [] }; trails.set(o.id, t); }
        t.cat = o.cat;
        t.s.push({ t: nowMs, x: w[0], y: w[1], z: w[2] });
        if (t.s.length > TRAIL_CAP) t.s.shift();
      }
    }
    if (snapshot && snapshot.satellite && snapshot.satellite.pos) {
      const s = snapshot.satellite;
      const w = worldFromMeters(s.pos);
      const key = 'sat:' + s.id;
      let t = trails.get(key);
      if (!t) { t = { cat: null, s: [] }; trails.set(key, t); } // cat null = always visible
      t.s.push({ t: nowMs, x: w[0], y: w[1], z: w[2] });
      if (t.s.length > TRAIL_CAP) t.s.shift();
    }
    // Prune trails not refreshed within the max trail window (+ margin).
    for (const [k, t] of trails) {
      const last = t.s[t.s.length - 1];
      if (!last || nowMs - last.t > 65000) trails.delete(k);
    }
  }

  function trailColorFor(cat) {
    if (cat == null) return SAT_COLOR;
    const c = CAT[cat];
    return c ? c.color : [1, 1, 1];
  }

  function rebuildTrailBuffer() {
    trailVertCount = 0;
    if (!settings.showTrails || !(settings.trailSeconds > 0)) return;
    const ref = lastSnapNow;
    const secs = settings.trailSeconds;

    // First pass: count qualifying segments (younger than trailSeconds, non-degenerate).
    let segCount = 0;
    for (const [, t] of trails) {
      if (!catVisible(t.cat)) continue;
      const s = t.s;
      for (let i = 1; i < s.length; i++) {
        if ((ref - s[i].t) / 1000 > secs) continue;
        const dx = s[i].x - s[i - 1].x;
        const dy = s[i].y - s[i - 1].y;
        const dz = s[i].z - s[i - 1].z;
        if (dx * dx + dy * dy + dz * dz < 1e-8) continue; // skip near-zero segments
        segCount++;
      }
    }
    if (segCount === 0) return;

    const floatsNeeded = segCount * 2 * 7;
    if (trailArr.length < floatsNeeded) {
      let cap = trailArr.length;
      while (cap < floatsNeeded) cap *= 2;
      trailArr = new Float32Array(cap);
    }

    // Second pass: fill interleaved [x,y,z, r,g,b,a] with per-vertex age fade.
    let k = 0;
    for (const [, t] of trails) {
      if (!catVisible(t.cat)) continue;
      const col = trailColorFor(t.cat);
      const s = t.s;
      for (let i = 1; i < s.length; i++) {
        const b = s[i];
        const a = s[i - 1];
        const ageB = (ref - b.t) / 1000;
        if (ageB > secs) continue;
        const dx = b.x - a.x;
        const dy = b.y - a.y;
        const dz = b.z - a.z;
        if (dx * dx + dy * dy + dz * dz < 1e-8) continue;
        const ageA = (ref - a.t) / 1000;
        let alphaA = 1 - ageA / secs; alphaA = alphaA < 0 ? 0 : alphaA > 1 ? 1 : alphaA;
        let alphaB = 1 - ageB / secs; alphaB = alphaB < 0 ? 0 : alphaB > 1 ? 1 : alphaB;
        trailArr[k++] = a.x; trailArr[k++] = a.y; trailArr[k++] = a.z;
        trailArr[k++] = col[0]; trailArr[k++] = col[1]; trailArr[k++] = col[2]; trailArr[k++] = alphaA;
        trailArr[k++] = b.x; trailArr[k++] = b.y; trailArr[k++] = b.z;
        trailArr[k++] = col[0]; trailArr[k++] = col[1]; trailArr[k++] = col[2]; trailArr[k++] = alphaB;
      }
    }
    trailVertCount = segCount * 2;
    gl.bindBuffer(gl.ARRAY_BUFFER, trailBuf);
    gl.bufferData(gl.ARRAY_BUFFER, trailArr.subarray(0, floatsNeeded), gl.DYNAMIC_DRAW);
  }

  // --- Draw helpers -------------------------------------------------------
  function disableAttribs() {
    for (let i = 0; i < 4; i++) gl.disableVertexAttribArray(i);
  }

  function drawEarth(mvp, sunDir) {
    gl.useProgram(earthProg);
    disableAttribs();
    gl.uniformMatrix4fv(earthU.mvp, false, mvp);
    gl.uniform3fv(earthU.sun, sunDir);
    gl.uniform1f(earthU.ambient, 0.25);
    gl.bindBuffer(gl.ARRAY_BUFFER, earthPosBuf);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, earthNrmBuf);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, earthIdxBuf);
    gl.disable(gl.BLEND);
    gl.depthMask(true);
    gl.drawElements(gl.TRIANGLES, earthIndexCount, gl.UNSIGNED_SHORT, 0);
  }

  function drawLines(buffer, vertCount, mvp) {
    if (vertCount <= 0) return;
    gl.useProgram(lineProg);
    disableAttribs();
    gl.uniformMatrix4fv(lineU.mvp, false, mvp);
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 28, 0);  // pos: 3 floats
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 4, gl.FLOAT, false, 28, 12); // rgba: 4 floats @ 12 bytes
    gl.enable(gl.BLEND);
    gl.depthMask(false);
    gl.drawArrays(gl.LINES, 0, vertCount);
  }

  function drawPoints(mvp) {
    if (objCount <= 0) return;
    gl.useProgram(pointsProg);
    disableAttribs();
    gl.uniformMatrix4fv(pointsU.mvp, false, mvp);
    gl.uniform1f(pointsU.dpr, dpr);
    gl.bindBuffer(gl.ARRAY_BUFFER, objPosBuf);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, objColBuf);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, objSizeBuf);
    gl.enableVertexAttribArray(2);
    gl.vertexAttribPointer(2, 1, gl.FLOAT, false, 0, 0);
    gl.enable(gl.BLEND);
    gl.depthMask(false);
    gl.drawArrays(gl.POINTS, 0, objCount);
  }

  function drawMarker(worldPos, sizePx, color, ring, mvp) {
    gl.useProgram(markerProg);
    disableAttribs();
    gl.uniformMatrix4fv(markerU.mvp, false, mvp);
    gl.uniform3fv(markerU.pos, worldPos);
    gl.uniform1f(markerU.size, sizePx * dpr);
    gl.uniform3fv(markerU.color, color);
    gl.uniform1f(markerU.ring, ring ? 1 : 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, markerBuf);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 1, gl.FLOAT, false, 0, 0);
    gl.enable(gl.BLEND);
    gl.depthMask(false);
    gl.drawArrays(gl.POINTS, 0, 1);
  }

  // Project a world point to CSS-pixel screen coordinates. Returns null if the
  // point is behind the camera.
  function projectToScreen(mvp, x, y, z) {
    const c = transform(mvp, [x, y, z, 1]);
    if (c[3] <= 1e-6) return null;
    const nx = c[0] / c[3];
    const ny = c[1] / c[3];
    return {
      sx: (nx * 0.5 + 0.5) * cssW,
      sy: (1 - (ny * 0.5 + 0.5)) * cssH,
    };
  }

  function drawOverlay(mvp, eye, sunPos) {
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0); // work in CSS px, clear device px
    ctx.clearRect(0, 0, cssW, cssH);
    ctx.textBaseline = 'middle';

    // Selection ring.
    if (selectedId != null) {
      const t = findRenderById(selectedId);
      if (t) {
        const p = projectToScreen(mvp, t.x, t.y, t.z);
        if (p) {
          ctx.beginPath();
          ctx.strokeStyle = '#ffd54a';
          ctx.lineWidth = 1.5;
          ctx.arc(p.sx, p.sy, 10, 0, Math.PI * 2);
          ctx.stroke();
        }
      }
    }

    // Labels: nearest 200 objects by camera distance + satellite + selection.
    if (settings.showLabels) {
      ctx.font = '11px monospace';
      ctx.fillStyle = 'rgba(220,228,238,0.92)';
      let list = renderObjects;
      if (renderObjects.length > 200) {
        const withD = renderObjects.map((o) => ({
          o,
          d: (o.x - eye[0]) ** 2 + (o.y - eye[1]) ** 2 + (o.z - eye[2]) ** 2,
        }));
        withD.sort((p, q) => p.d - q.d);
        list = [];
        for (let i = 0; i < 200; i++) list.push(withD[i].o);
      }
      for (const o of list) {
        const p = projectToScreen(mvp, o.x, o.y, o.z);
        if (p) ctx.fillText(String(o.id), p.sx + 6, p.sy);
      }
      if (satRender) {
        const p = projectToScreen(mvp, satRender.x, satRender.y, satRender.z);
        if (p) {
          ctx.fillStyle = '#ffd54a';
          ctx.fillText(String(satRender.id), p.sx + 6, p.sy);
          ctx.fillStyle = 'rgba(220,228,238,0.92)';
        }
      }
      if (selectedId != null) {
        const t = findRenderById(selectedId);
        if (t) {
          const p = projectToScreen(mvp, t.x, t.y, t.z);
          if (p) ctx.fillText(String(t.id), p.sx + 6, p.sy);
        }
      }
    }

    // Sun label near the disc if on-screen (always, independent of labels).
    const sp = projectToScreen(mvp, sunPos[0], sunPos[1], sunPos[2]);
    if (sp && sp.sx > -20 && sp.sx < cssW + 20 && sp.sy > -20 && sp.sy < cssH + 20) {
      ctx.font = '11px monospace';
      ctx.fillStyle = 'rgba(255,235,180,0.95)';
      ctx.fillText('Sun ●', sp.sx + 8, sp.sy);
    }
  }

  // --- Public API ---------------------------------------------------------
  function resize() {
    dpr =
      typeof window !== 'undefined' && window.devicePixelRatio
        ? window.devicePixelRatio
        : 1;
    const w = glCanvas.clientWidth || glCanvas.width || 1;
    const h = glCanvas.clientHeight || glCanvas.height || 1;
    cssW = w;
    cssH = h;
    const gw = Math.max(1, Math.round(w * dpr));
    const gh = Math.max(1, Math.round(h * dpr));
    glCanvas.width = gw;
    glCanvas.height = gh;
    overlayCanvas.width = gw;
    overlayCanvas.height = gh;
    gl.viewport(0, 0, gw, gh);
    proj = perspective(FOV, gw / gh, NEAR, FAR);
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  }

  function setSnapshot(snap, nowMs) {
    snapshot = snap || null;
    lastSnapNow = typeof nowMs === 'number' ? nowMs : Date.now();
    appendTrails(lastSnapNow);
    rebuildObjectBuffer();
    rebuildSatellite();
    rebuildTrailBuffer();
  }

  function setSettings(s) {
    settings = normalizeSettings(s);
    rebuildObjectBuffer();
    rebuildTrailBuffer();
  }

  function setSelected(idOrNull) {
    selectedId = idOrNull == null ? null : idOrNull;
  }

  function frame(nowMs) {
    const now = typeof nowMs === 'number' ? nowMs : Date.now();
    const view = camera.getViewMatrix();
    const mvp = multiply(proj, view);
    const eye = camera.getEye();
    const sunDir = normalize(sunDirectionEcef(now));
    const sunPos = [sunDir[0] * SUN_DIST, sunDir[1] * SUN_DIST, sunDir[2] * SUN_DIST];

    gl.viewport(0, 0, glCanvas.width, glCanvas.height);
    gl.clearColor(0x05 / 255, 0x07 / 255, 0x0d / 255, 1); // near-black #05070d
    gl.enable(gl.DEPTH_TEST);
    gl.depthFunc(gl.LEQUAL);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    gl.depthMask(true);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);

    drawEarth(mvp, sunDir);
    drawLines(gratBuf, gratVertCount, mvp);
    drawMarker(sunPos, 42, SUN_COLOR, false, mvp);
    if (settings.showTrails) drawLines(trailBuf, trailVertCount, mvp);
    drawPoints(mvp);
    if (velActive) drawLines(velBuf, 2, mvp);
    if (satRender) drawMarker([satRender.x, satRender.y, satRender.z], 24, SAT_COLOR, true, mvp);

    drawOverlay(mvp, eye, sunPos);
  }

  function pick(x, y) {
    const view = camera.getViewMatrix();
    const mvp = multiply(proj, view);
    const eye = camera.getEye();
    let bestId = null;
    let bestD = Infinity;
    let bestCam = Infinity;
    const consider = (o) => {
      const p = projectToScreen(mvp, o.x, o.y, o.z);
      if (!p) return;
      const d = Math.hypot(p.sx - x, p.sy - y);
      if (d > 12) return;
      const cam = (o.x - eye[0]) ** 2 + (o.y - eye[1]) ** 2 + (o.z - eye[2]) ** 2;
      if (d < bestD - 1e-9 || (Math.abs(d - bestD) < 1e-9 && cam < bestCam)) {
        bestD = d;
        bestCam = cam;
        bestId = o.id;
      }
    };
    if (satRender) consider(satRender);
    for (let i = 0; i < renderObjects.length; i++) consider(renderObjects[i]);
    return bestId;
  }

  function dispose() {
    camera.dispose();
  }

  return { resize, setSnapshot, setSettings, setSelected, frame, pick, dispose };
}
