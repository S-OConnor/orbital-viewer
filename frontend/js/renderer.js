// renderer.js — custom minimal WebGL 1 renderer for the Orbital LOS Viewer.
//
// Draws: a textured lat/lon Earth globe (NASA day/night imagery via textures.js
// with a procedural fallback until it loads) + 15-degree graticule, an
// approximate Sun billboard (which also lights the globe), all tracked objects
// as a single textured GL_POINTS draw of atlas icon sprites, the primary
// satellite marker (satMarker atlas art) + 60 s velocity vector, age-faded
// GL_LINES trails (client-built at the ~1 Hz snapshot rate, or server-fed from
// OLV2 `trailPoints` deltas at source sample rate — docs/PROTOCOL_WS.md), and
// 2D-canvas labels / selection ring on the overlay.
//
// Two view modes (settings.viewMode, docs/FEATURE_SATVIEW.md): the default
// 'orbit' free-orbit camera, and 'sat' — a first-person view from the primary
// satellite looking at nadir, rendered with an equidistant fisheye projection
// (170 deg FOV) so the Earth disc plus the surrounding space fit the frame.
// Every vertex shader carries both paths, selected per-draw by the uFisheye
// uniform; the fisheye forward map is kept LOCKSTEP with sat_camera.js.
//
// Public API (frozen — docs/PLAN.md section 6):
//   createRenderer(glCanvas, overlayCanvas) -> {
//     resize(), setSnapshot(snap, nowMs), setSettings(s),
//     setSelected(idOrNull), frame(nowMs), pick(x, y) -> id|null }
//   setSettings additionally accepts viewMode: 'orbit' | 'sat' (additive per
//   FEATURE_SATVIEW.md §5.1; anything not exactly 'sat' normalizes to 'orbit').
//   No new public methods.
//
// Units: 1 unit = 1,000 km (SCALE = 1e-6 m -> units). Earth radius 6.371 units,
// centred at the origin, ECEF axes with +Z toward the north pole.
//
// IMPORT-SAFE: no DOM / WebGL access happens at import time; everything runs
// inside createRenderer().

import { perspective, multiply, transform, normalize, occludedBySphere } from './math3.js';
import { createCamera } from './camera.js';
import { sunDirectionEcef } from './sun.js';
// Sibling modules on frozen interfaces (v0.2). Both are IMPORT-SAFE: their
// exports are only *called* inside createRenderer(), never at import time.
import { ATLAS_GRID, CELL, ICONS, paintAtlas, iconUV } from './atlas.js';
import { createEarthTextures } from './textures.js';
// Satellite-POV fisheye view (frozen interface — docs/FEATURE_SATVIEW.md §3).
// IMPORT-SAFE pure-logic sibling: its exports are only *called* inside
// createRenderer()/frame()/pick(), never at import time.
import { SAT_FOV_DEG, SAT_NEAR, SAT_FAR, satViewMatrix, fisheyeProjectNdc } from './sat_camera.js';
// Celestial background ephemerides + catalog (frozen interface — docs/
// FEATURE_SKY.md §4). IMPORT-SAFE pure math: STAR_COUNT/STAR_MAGS are filled
// at import, but the direction functions are only *called* inside
// createRenderer()/rebuildSky(), never at import time.
import {
  moonDirectionEcef,
  moonIlluminatedFraction,
  planetsEcef,
  starsEcefInto,
  STAR_COUNT,
  STAR_MAGS,
  PLANET_NAMES,
} from './sky.js';

const DEG = Math.PI / 180;
const SCALE = 1e-6;      // metres -> scene units (1 unit = 1,000 km)
const EARTH_R = 6.371;   // Earth radius in units
// Occlusion radius for overlay labels/picking: a hair inside EARTH_R so
// near-side surface objects and the tessellated sphere's chord sag don't
// falsely hide anything.
const OCCLUDE_R = EARTH_R * 0.999;
const SHELL = 120;       // distant objects (stars) clamp onto this radius
const MAX_OBJECTS = 5000;
const TRAIL_CAP = 64;    // ring-buffer samples kept per object
// Server-fed trails (OLV2 `trailPoints`, docs/PROTOCOL_WS.md) use a much larger
// cap than client-built ones: a 10 Hz track over a 30 s trailSeconds window
// needs ~300 samples, and TRAIL_CAP=64 would truncate that to 6.4 s.
const SERVER_TRAIL_CAP = 512;
const SUN_DIST = 150;    // Sun billboard distance in units
const SKY_DIST = 300;    // celestial-background shell (stars/Moon/planets), < FAR

const FOV = 45 * DEG;
const NEAR = 0.5;
const FAR = 1000;

// Half-FOV in radians for the fisheye path (uThetaMax): the equidistant model
// maps theta == SAT_THETA_MAX to NDC radius 1. LOCKSTEP with sat_camera.js §4.2
// (thetaMax = (fovDeg/2) * pi/180) — derived from the imported SAT_FOV_DEG.
const SAT_THETA_MAX = (SAT_FOV_DEG / 2) * DEG;

// Category appearance. groundHot colour is computed per-object from intensity.
// size is in CSS px (multiplied by DPR in the points shader, then clamped to the
// GPU point-size cap). icon selects the atlas cell (ICONS.*); cells 0..4 are
// white masks (as is ICONS.unknown) that get tinted by the per-object colour in
// the fragment shader.
const CAT = {
  debris:    { color: [0xaa / 255, 0xb2 / 255, 0xbd / 255], size: 10, icon: ICONS.debris },    // #aab2bd
  star:      { color: [1, 1, 1],                             size: 9,  icon: ICONS.star },      // #ffffff
  comet:     { color: [0x6f / 255, 0xd3 / 255, 0xff / 255], size: 14, icon: ICONS.comet },     // #6fd3ff
  satellite: { color: [0x58 / 255, 0xd6 / 255, 0x8d / 255], size: 14, icon: ICONS.satellite }, // #58d68d
  groundHot: { color: [1, 0.5647, 0.251],                   size: 13, icon: ICONS.groundHot },  // base (unused directly)
  unknown:   { color: [0xc7 / 255, 0x92 / 255, 0xea / 255], size: 12, icon: ICONS.unknown },   // #c792ea
};
const SAT_COLOR = [1, 0.8353, 0.2902]; // #ffd54a primary satellite
const SUN_COLOR = [1.0, 0.93, 0.7];    // warm white/yellow disc

// Celestial-background appearance (docs/FEATURE_SKY.md §8.2). Planet tints keyed
// by sky.js PLANET_NAMES; each is a white-mask skyDot sprite multiplied by this
// [r,g,b] so the dot reads as the planet's characteristic colour. The Moon is a
// grey-white soft marker disc (like the Sun, uUseAtlas 0).
const PLANET_TINT = {
  mercury: [0xc9 / 255, 0xb8 / 255, 0xa0 / 255], // #c9b8a0
  venus:   [0xf5 / 255, 0xf3 / 255, 0xe0 / 255], // #f5f3e0
  mars:    [0xe0 / 255, 0x66 / 255, 0x3c / 255], // #e0663c
  jupiter: [0xe3 / 255, 0xd2 / 255, 0xa8 / 255], // #e3d2a8
  saturn:  [0xe6 / 255, 0xcf / 255, 0x8f / 255], // #e6cf8f
};
const MOON_COLOR = [0xd8 / 255, 0xd8 / 255, 0xd0 / 255]; // #d8d8d0 grey-white
const MOON_SIZE_PX = 26;                                 // Moon disc size (CSS px)

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

// Apparent magnitude -> on-screen appearance for the celestial background
// (cosmetic, renderer-owned). Astronomy convention: LOWER (more negative)
// magnitude == brighter, so brighter bodies map to bigger + whiter. All values
// are tuned to taste for a legible background, not photometric accuracy.
function clamp(v, lo, hi) { return v < lo ? lo : v > hi ? hi : v; }
function starSizeForMag(mag) { return clamp(5.6 - 0.85 * mag, 2.2, 7.0); }   // CSS px
function starBrightness(mag) { return clamp(1.3 - 0.16 * mag, 0.5, 1.0); }   // grey level
function planetSizeForMag(mag) { return clamp(7.5 - 0.7 * mag, 4.5, 11); }  // CSS px, > stars
function planetTintScale(mag) { return clamp(0.7 - 0.05 * mag, 0.5, 1.0); }  // keep planets vivid

// ---------------------------------------------------------------------------
// Shader sources
// ---------------------------------------------------------------------------

// Equidistant-fisheye vertex transform, shared VERBATIM by all four vertex
// shaders (prepended below) and selected per-draw by the uFisheye uniform. This
// is the GPU half of the sat-view projection; it MUST stay byte-for-byte
// equivalent to sat_camera.js fisheyeProjectNdc() (docs/FEATURE_SATVIEW.md
// §4.2 / §5.2) or picking and labels will disagree with what the GPU draws.
// thetaMax / aspect / depthRange arrive as params so the function holds no state.
const FISHEYE_GLSL = `
// Equidistant fisheye — MUST match sat_camera.js fisheyeProjectNdc().
vec4 fisheyePosition(vec4 viewPos, float thetaMax, float aspect, vec2 depthRange) {
  float rho = length(viewPos.xyz);
  if (rho < 1e-9) return vec4(0.0, 0.0, -3.0, 1.0);          // at eye: cull
  vec3 dir = viewPos.xyz / rho;
  float theta = acos(clamp(-dir.z, -1.0, 1.0));
  float depth = clamp((rho - depthRange.x) / (depthRange.y - depthRange.x), 0.0, 1.0) * 2.0 - 1.0;
  float pLen = length(dir.xy);
  if (pLen < 1e-6) {
    if (theta > 1.5707963) return vec4(0.0, 0.0, -3.0, 1.0); // behind: cull
    return vec4(0.0, 0.0, depth, 1.0);                        // boresight
  }
  float r = theta / thetaMax;
  return vec4(r * (dir.x / pLen) / aspect, r * (dir.y / pLen), depth, 1.0);
}`;

// UV is precomputed CPU-side (exact, seam-safe) and carried through a HIGHP
// varying: a 4096-wide equirect texture needs more than mediump interpolation to
// avoid visible banding, so we resolve it in the vertex shader and pass highp.
const EARTH_VS = `${FISHEYE_GLSL}
attribute vec3 aPos;
attribute vec3 aNormal;
attribute vec2 aUV;
uniform mat4 uMVP;
uniform bool uFisheye;                          // false: orbit uMVP; true: fisheye
uniform mat4 uView;                             // sat_camera.js view matrix
uniform float uThetaMax;
uniform float uAspect;
uniform vec2 uDepthRange;                        // [SAT_NEAR, SAT_FAR]
varying vec3 vNormal;
varying vec3 vPos;
varying highp vec2 vUV;
void main() {
  vNormal = aNormal;
  vPos = aPos;
  vUV = aUV;
  if (uFisheye) {
    gl_Position = fisheyePosition(uView * vec4(aPos, 1.0), uThetaMax, uAspect, uDepthRange);
  } else {
    gl_Position = uMVP * vec4(aPos, 1.0);
  }
}`;

// Textured Earth with a graceful fallback. When the async day texture is ready
// (uHasDay) we shade it Lambert-style; when the night-lights texture is also
// ready (uHasNight) we add emissive city lights on the anti-solar hemisphere and
// a faint blue atmospheric fresnel rim. Until then — first frames, or a texture
// load failure — we reproduce the EXACT v0.1 procedural deep-blue + latitude-band
// look so nothing regresses visually.
const EARTH_FS = `
precision mediump float;
varying vec3 vNormal;
varying vec3 vPos;
varying highp vec2 vUV;
uniform vec3 uSunDir;
uniform float uAmbient;
uniform vec3 uEye;
uniform bool uHasDay;
uniform bool uHasNight;
uniform sampler2D uDay;
uniform sampler2D uNight;
void main() {
  vec3 N = normalize(vNormal);
  vec3 L = normalize(uSunDir);
  float ndl = dot(N, L);
  float diff = max(ndl, 0.0);
  float light = uAmbient + (1.0 - uAmbient) * diff;
  vec3 color;
  if (uHasDay) {
    color = texture2D(uDay, vUV).rgb * light;
    if (uHasNight) {
      // City lights ramp across the terminator: 0 on the lit side (ndl >= 0.05),
      // full on the deep night side (ndl <= -0.18). Both edges tuned to taste.
      float nightMix = smoothstep(0.05, -0.18, ndl);
      color += texture2D(uNight, vUV).rgb * nightMix;
    }
    // Subtle blue atmospheric rim: brightest at the silhouette, additive.
    vec3 V = normalize(uEye - vPos);
    float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    color += vec3(0.20, 0.38, 0.66) * rim * 0.55;
  } else {
    // Procedural fallback — byte-for-byte identical to the v0.1 shading.
    float lat = vPos.z / 6.371;                 // -1 (south) .. +1 (north)
    vec3 base = vec3(0.05, 0.19, 0.44);         // deep blue
    base += vec3(0.015, 0.03, 0.05) * cos(lat * 9.0); // subtle latitude bands
    color = base * light;
  }
  gl_FragColor = vec4(color, 1.0);
}`;

const LINE_VS = `${FISHEYE_GLSL}
attribute vec3 aPos;
attribute vec4 aColor;
uniform mat4 uMVP;
uniform bool uFisheye;
uniform mat4 uView;
uniform float uThetaMax;
uniform float uAspect;
uniform vec2 uDepthRange;
varying vec4 vColor;
void main() {
  vColor = aColor;
  if (uFisheye) {
    gl_Position = fisheyePosition(uView * vec4(aPos, 1.0), uThetaMax, uAspect, uDepthRange);
  } else {
    gl_Position = uMVP * vec4(aPos, 1.0);
  }
}`;

const LINE_FS = `
precision mediump float;
varying vec4 vColor;
void main() { gl_FragColor = vColor; }`;

// Object sprites. aIcon selects the atlas cell; the VS derives that cell's UV
// rect using the SAME 4x4 grid + inset math as atlas.js iconUV() (which cannot be
// called from GLSL). ATLAS_GRID and INSET below MUST stay in lockstep with
// atlas.js — if the frozen inset fraction changes there, change it here too.
const POINTS_VS = `${FISHEYE_GLSL}
attribute vec3 aPos;
attribute vec3 aColor;
attribute float aSize;
attribute float aIcon;
uniform mat4 uMVP;
uniform bool uFisheye;
uniform mat4 uView;
uniform float uThetaMax;
uniform float uAspect;
uniform vec2 uDepthRange;
uniform float uDpr;
uniform float uMaxPoint;                       // ALIASED_POINT_SIZE_RANGE[1] cap
varying vec3 vColor;
varying vec2 vUv0;
varying vec2 vUv1;
const float ATLAS_GRID = 4.0;                  // == atlas.js ATLAS_GRID
const float INSET = 0.04;                      // == atlas.js iconUV inset (~4%)
void main() {
  vColor = aColor;
  float cell = 1.0 / ATLAS_GRID;
  vec2 rc = vec2(mod(aIcon, ATLAS_GRID), floor(aIcon / ATLAS_GRID)); // col,row
  vUv0 = (rc + INSET) * cell;                  // image-space rect min (v=0 = top)
  vUv1 = (rc + 1.0 - INSET) * cell;            // image-space rect max
  if (uFisheye) {
    gl_Position = fisheyePosition(uView * vec4(aPos, 1.0), uThetaMax, uAspect, uDepthRange);
  } else {
    gl_Position = uMVP * vec4(aPos, 1.0);
  }
  gl_PointSize = min(aSize * uDpr, uMaxPoint); // CSS px -> device px, clamped
}`;

const POINTS_FS = `
precision mediump float;
varying vec3 vColor;
varying vec2 vUv0;
varying vec2 vUv1;
uniform sampler2D uAtlas;
void main() {
  // gl_PointCoord origin is top-left in WebGL, matching image-space v=0 = top,
  // so no flip is needed (atlas uploaded with UNPACK_FLIP_Y false).
  vec2 uv = mix(vUv0, vUv1, gl_PointCoord);
  vec4 t = texture2D(uAtlas, uv);
  if (t.a < 0.05) discard;                     // trim transparent icon margin
  gl_FragColor = vec4(t.rgb * vColor, t.a);    // white mask * category tint
}`;

// Single-point billboard used for the Sun disc and the satellite marker.
// aVertex is a dummy attribute (always fed 0) that keeps vertex attribute 0
// enabled — some GL backends require attrib 0 to be a real array for a draw.
const MARKER_VS = `${FISHEYE_GLSL}
attribute float aVertex;
uniform mat4 uMVP;
uniform bool uFisheye;
uniform mat4 uView;
uniform float uThetaMax;
uniform float uAspect;
uniform vec2 uDepthRange;
uniform vec3 uWorldPos;
uniform float uSize;
void main() {
  if (uFisheye) {
    gl_Position = fisheyePosition(uView * vec4(uWorldPos, 1.0), uThetaMax, uAspect, uDepthRange);
  } else {
    gl_Position = uMVP * vec4(uWorldPos, 1.0);
  }
  gl_PointSize = uSize + aVertex; // aVertex is 0, keeps the attribute live
}`;

// The Sun keeps the soft procedural disc (uUseAtlas 0); the primary satellite
// samples the full-colour satMarker art from the atlas (uUseAtlas 1), untinted,
// replacing the old disc+ring blob. uUv0/uUv1 are the satMarker cell rect from
// atlas.js iconUV() (image space, v=0 = top — matches gl_PointCoord's top-left).
const MARKER_FS = `
precision mediump float;
uniform vec3 uColor;
uniform bool uUseAtlas;
uniform sampler2D uAtlas;
uniform vec2 uUv0;
uniform vec2 uUv1;
void main() {
  if (uUseAtlas) {
    vec2 uv = mix(uUv0, uUv1, gl_PointCoord);
    vec4 t = texture2D(uAtlas, uv);
    if (t.a < 0.05) discard;
    gl_FragColor = t;                          // full-colour art, sampled untinted
  } else {
    vec2 c = gl_PointCoord - 0.5;
    float r = length(c) * 2.0;                 // 0 centre .. ~1 edge
    if (r > 1.0) discard;
    float a = smoothstep(1.0, 0.0, r);
    gl_FragColor = vec4(uColor, a);            // soft disc (Sun)
  }
}`;

// ---------------------------------------------------------------------------
// Geometry builders (pure)
// ---------------------------------------------------------------------------

function buildSphere(R, stacks, slices) {
  const positions = [];
  const normals = [];
  const uvs = [];
  const indices = [];
  for (let i = 0; i <= stacks; i++) {
    const phi = (i / stacks) * Math.PI - Math.PI / 2; // -pi/2 .. +pi/2 latitude
    const cp = Math.cos(phi);
    const sp = Math.sin(phi);
    // Equirect v: phi=+pi/2 (north pole) -> 0 (image top); -pi/2 (south) -> 1.
    const v = 0.5 - phi / Math.PI;
    for (let j = 0; j <= slices; j++) {
      const theta = (j / slices) * 2 * Math.PI;       // 0 .. 2pi longitude
      const nx = cp * Math.cos(theta);
      const ny = cp * Math.sin(theta);
      const nz = sp;                                   // +Z north pole
      positions.push(nx * R, ny * R, nz * R);
      normals.push(nx, ny, nz);
      // Equirect u: Greenwich (theta=0) -> 0.5, monotonic 0.5 -> 1.5 per ring.
      // WRAP_S=REPEAT makes the fract() step at the antimeridian seamless; the
      // duplicated last column (j==slices, u=1.5) closes the mesh at Greenwich.
      uvs.push(0.5 + theta / (2 * Math.PI), v);
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
    uvs: new Float32Array(uvs),
    indices: new Uint16Array(indices),
  };
}

// Interleaved [x,y,z, r,g,b,a] line vertices for a 15-degree graticule.
function buildGraticule(R) {
  const r = R * 1.003; // slightly raised to avoid z-fighting with the globe
  const col = [0.55, 0.72, 1.0, 0.14]; // subtle reference grid over the texture (was 0.22)
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
  const glAttrs = { antialias: true }; // MSAA on the default framebuffer (anti-pixelation)
  const gl =
    glCanvas.getContext('webgl', glAttrs) ||
    glCanvas.getContext('experimental-webgl', glAttrs);
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

  const earthProg = program(EARTH_VS, EARTH_FS, { aPos: 0, aNormal: 1, aUV: 2 });
  const lineProg = program(LINE_VS, LINE_FS, { aPos: 0, aColor: 1 });
  const pointsProg = program(POINTS_VS, POINTS_FS, { aPos: 0, aColor: 1, aSize: 2, aIcon: 3 });
  const markerProg = program(MARKER_VS, MARKER_FS, { aVertex: 0 });

  // The five fisheye uniforms live on every vertex shader (FISHEYE_GLSL + the
  // per-shader branch); look them up uniformly so the four programs never drift.
  // setTransformUniforms() feeds these (or uMVP) per draw.
  function fisheyeUniforms(prog) {
    return {
      fisheye: gl.getUniformLocation(prog, 'uFisheye'),
      view: gl.getUniformLocation(prog, 'uView'),
      thetaMax: gl.getUniformLocation(prog, 'uThetaMax'),
      aspect: gl.getUniformLocation(prog, 'uAspect'),
      depthRange: gl.getUniformLocation(prog, 'uDepthRange'),
    };
  }

  const earthU = {
    mvp: gl.getUniformLocation(earthProg, 'uMVP'),
    ...fisheyeUniforms(earthProg),
    sun: gl.getUniformLocation(earthProg, 'uSunDir'),
    ambient: gl.getUniformLocation(earthProg, 'uAmbient'),
    eye: gl.getUniformLocation(earthProg, 'uEye'),
    hasDay: gl.getUniformLocation(earthProg, 'uHasDay'),
    hasNight: gl.getUniformLocation(earthProg, 'uHasNight'),
    day: gl.getUniformLocation(earthProg, 'uDay'),
    night: gl.getUniformLocation(earthProg, 'uNight'),
  };
  const lineU = { mvp: gl.getUniformLocation(lineProg, 'uMVP'), ...fisheyeUniforms(lineProg) };
  const pointsU = {
    mvp: gl.getUniformLocation(pointsProg, 'uMVP'),
    ...fisheyeUniforms(pointsProg),
    dpr: gl.getUniformLocation(pointsProg, 'uDpr'),
    maxPoint: gl.getUniformLocation(pointsProg, 'uMaxPoint'),
    atlas: gl.getUniformLocation(pointsProg, 'uAtlas'),
  };
  const markerU = {
    mvp: gl.getUniformLocation(markerProg, 'uMVP'),
    ...fisheyeUniforms(markerProg),
    pos: gl.getUniformLocation(markerProg, 'uWorldPos'),
    size: gl.getUniformLocation(markerProg, 'uSize'),
    color: gl.getUniformLocation(markerProg, 'uColor'),
    useAtlas: gl.getUniformLocation(markerProg, 'uUseAtlas'),
    atlas: gl.getUniformLocation(markerProg, 'uAtlas'),
    uv0: gl.getUniformLocation(markerProg, 'uUv0'),
    uv1: gl.getUniformLocation(markerProg, 'uUv1'),
  };

  // --- Static geometry buffers -------------------------------------------
  // 64x96 tessellation -> 65*97 = 6305 verts, 64*96*6 = 36864 indices (< 65536,
  // safe for Uint16). Higher than v0.1's 32x48 to smooth the textured silhouette.
  const sphere = buildSphere(EARTH_R, 64, 96);
  const earthPosBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, earthPosBuf);
  gl.bufferData(gl.ARRAY_BUFFER, sphere.positions, gl.STATIC_DRAW);
  const earthNrmBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, earthNrmBuf);
  gl.bufferData(gl.ARRAY_BUFFER, sphere.normals, gl.STATIC_DRAW);
  const earthUvBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, earthUvBuf);
  gl.bufferData(gl.ARRAY_BUFFER, sphere.uvs, gl.STATIC_DRAW);
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
  const iconArr = new Float32Array(MAX_OBJECTS); // atlas cell index per object
  const objPosBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, objPosBuf);
  gl.bufferData(gl.ARRAY_BUFFER, posArr, gl.DYNAMIC_DRAW);
  const objColBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, objColBuf);
  gl.bufferData(gl.ARRAY_BUFFER, colArr, gl.DYNAMIC_DRAW);
  const objSizeBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, objSizeBuf);
  gl.bufferData(gl.ARRAY_BUFFER, sizeArr, gl.DYNAMIC_DRAW);
  const objIconBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, objIconBuf);
  gl.bufferData(gl.ARRAY_BUFFER, iconArr, gl.DYNAMIC_DRAW);
  let objCount = 0;

  // --- Sky buffers (stars, Moon, planets) ---------------------------------
  // The celestial background reuses EXISTING programs so it needs no new GLSL
  // (and stays lockstep-safe with the sat-view fisheye path, docs/FEATURE_SKY.md
  // §2): stars and planets draw through pointsProg as GL_POINTS with
  // aIcon = ICONS.skyDot (the soft round dot), and the Moon reuses drawMarker()'s
  // soft-disc path — exactly like the Sun.
  //
  // Star colour + size depend ONLY on magnitude, so they are built ONCE here
  // from STAR_MAGS; only the star POSITIONS change with time (the sky wheels by
  // GMST), rebuilt each setSnapshot in rebuildSky(). A single per-star aIcon
  // buffer holds ICONS.skyDot for every star.
  const starColArr = new Float32Array(STAR_COUNT * 3);
  const starSizeArr = new Float32Array(STAR_COUNT);
  const starIconArr = new Float32Array(STAR_COUNT);
  for (let i = 0; i < STAR_COUNT; i++) {
    const b = starBrightness(STAR_MAGS[i]);
    starColArr[i * 3] = b;                 // greyish-white: dimmer stars -> greyer
    starColArr[i * 3 + 1] = b;
    starColArr[i * 3 + 2] = Math.min(1, b * 1.05); // a hair cooler than neutral
    starSizeArr[i] = starSizeForMag(STAR_MAGS[i]);
    starIconArr[i] = ICONS.skyDot;
  }
  const starColBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, starColBuf);
  gl.bufferData(gl.ARRAY_BUFFER, starColArr, gl.STATIC_DRAW);
  const starSizeBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, starSizeBuf);
  gl.bufferData(gl.ARRAY_BUFFER, starSizeArr, gl.STATIC_DRAW);
  const starIconBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, starIconBuf);
  gl.bufferData(gl.ARRAY_BUFFER, starIconArr, gl.STATIC_DRAW);

  // Star positions: scratch (unit ECEF from sky.js, then scaled to SKY_DIST)
  // and a DYNAMIC_DRAW GL buffer refilled each setSnapshot — no per-call heap
  // allocation (starsEcefInto writes straight into starUnit).
  const starUnit = new Float32Array(STAR_COUNT * 3);
  const starPos = new Float32Array(STAR_COUNT * 3);
  const starPosBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, starPosBuf);
  gl.bufferData(gl.ARRAY_BUFFER, starPos, gl.DYNAMIC_DRAW);

  // Planets: up to PLANET_NAMES.length points. pos/colour/size are rebuilt each
  // setSnapshot from planetsEcef(); aIcon is a constant skyDot per planet.
  const PLANET_CAP = PLANET_NAMES.length;
  const planetPosArr = new Float32Array(PLANET_CAP * 3);
  const planetColArr = new Float32Array(PLANET_CAP * 3);
  const planetSizeArr = new Float32Array(PLANET_CAP);
  const planetIconArr = new Float32Array(PLANET_CAP);
  for (let i = 0; i < PLANET_CAP; i++) planetIconArr[i] = ICONS.skyDot;
  const planetPosBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, planetPosBuf);
  gl.bufferData(gl.ARRAY_BUFFER, planetPosArr, gl.DYNAMIC_DRAW);
  const planetColBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, planetColBuf);
  gl.bufferData(gl.ARRAY_BUFFER, planetColArr, gl.DYNAMIC_DRAW);
  const planetSizeBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, planetSizeBuf);
  gl.bufferData(gl.ARRAY_BUFFER, planetSizeArr, gl.DYNAMIC_DRAW);
  const planetIconBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, planetIconBuf);
  gl.bufferData(gl.ARRAY_BUFFER, planetIconArr, gl.STATIC_DRAW);
  let planetCount = 0;
  const planetRender = []; // [{name, x, y, z}] for the overlay labels
  let moonRender = null;   // {x, y, z, fraction} in scene units, or null

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

  // --- Textures -----------------------------------------------------------
  // Icon atlas (SYNCHRONOUS): a power-of-two canvas painted by atlas.js and
  // uploaded once with mipmaps. atlasScale keeps the canvas POT — 256 px at
  // DPR < 1.5, 512 px otherwise (ATLAS_GRID*CELL == 256; scale 1 or 2 only) — so
  // generateMipmap + LINEAR_MIPMAP_LINEAR are valid. UNPACK_FLIP_Y is left false
  // so the uploaded texture's v=0 row is the image top, matching iconUV()/
  // gl_PointCoord. We are inside createRenderer (a real GL canvas is required),
  // so document.createElement is safe to assume here.
  const atlasDpr =
    typeof window !== 'undefined' && window.devicePixelRatio ? window.devicePixelRatio : 1;
  const atlasScale = Math.round(Math.min(atlasDpr, 2)); // 1 or 2 -> 256 or 512 px (POT)
  const atlasCanvas = document.createElement('canvas');
  atlasCanvas.width = ATLAS_GRID * CELL * atlasScale;
  atlasCanvas.height = ATLAS_GRID * CELL * atlasScale;
  paintAtlas(atlasCanvas.getContext('2d'), CELL * atlasScale);
  const atlasTex = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, atlasTex);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
  gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, atlasCanvas);
  gl.generateMipmap(gl.TEXTURE_2D);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  const satUV = iconUV(ICONS.satMarker); // fixed satellite-art cell (source of truth)

  // Equirectangular Earth day/night textures (ASYNC). textures.js resolves the
  // vendored NASA assets relative to itself and uploads them mipmapped; the
  // handle's day/night fields (and version counter) stay null/0 until the images
  // arrive, so frame() polls them and drawEarth switches shader paths on the fly.
  const earthTex = createEarthTextures(gl);

  // gl_PointSize is capped by the GPU (commonly 63/64 device px). Query once and
  // clamp every point draw (uMaxPoint in the VS; Math.min for the marker).
  const psRange = gl.getParameter(gl.ALIASED_POINT_SIZE_RANGE);
  const maxPointSize = psRange && psRange[1] ? psRange[1] : 64;

  // Fixed sampler -> texture-unit bindings, set once after link (persist per
  // program): earth day = unit 0, night = unit 1, atlas = unit 2.
  gl.useProgram(earthProg);
  gl.uniform1i(earthU.day, 0);
  gl.uniform1i(earthU.night, 1);
  gl.useProgram(pointsProg);
  gl.uniform1i(pointsU.atlas, 2);
  gl.uniform1f(pointsU.maxPoint, maxPointSize);
  gl.useProgram(markerProg);
  gl.uniform1i(markerU.atlas, 2);

  // --- Renderer state -----------------------------------------------------
  let settings = normalizeSettings(null);
  let snapshot = null;
  let selectedId = null;
  let lastSnapNow = 0;
  // Scene epoch (docs/FEATURE_SKY.md §8.1): one wall-clock time source for the
  // Sun, Moon, planets and star GMST, derived from the state message's
  // serverTime. sceneEpochMs is the epoch as Unix ms; sceneEpochBaseNow is the
  // frame/snapshot clock value it was anchored at (NaN until the first frame
  // anchors it), so sceneNowMs() can advance the epoch smoothly between the
  // ~1 Hz snapshots. Defaults to Date.now() so the sky still renders (and the
  // Sun is placed correctly) before any snapshot arrives.
  let sceneEpochMs = Date.now();
  let sceneEpochBaseNow = NaN;
  let cssW = 1;
  let cssH = 1;
  let dpr = 1;
  let proj = perspective(FOV, 1, NEAR, FAR);

  const renderObjects = []; // {id, cat, x, y, z} for visible objects (units, clamped)
  let satRender = null;     // {id, x, y, z} or null
  // id | 'sat:<id>' -> {cat, s:[{t,x,y,z}], server?}. `server: true` marks a
  // trail fed from OLV2 trailPoints (docs/PROTOCOL_WS.md); such trails skip
  // the 1 Hz snapshot-position append (appendTrails) to avoid duplicates.
  const trails = new Map();

  // Active-view decision for the current frame()/pick(), set at the top of each.
  // {fisheye, view, mvp, eye, aspectGl, aspectCss} — the draw helpers, overlay
  // and projectToScreen all read it so GPU, labels and picking never disagree.
  let activeView = null;

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
      // Celestial background toggle (docs/FEATURE_SKY.md §8.2). Default ON:
      // absent/undefined -> true, only an explicit false disables the sky.
      showSky: s.showSky !== false,
      // Additive per FEATURE_SATVIEW.md §5.1: anything not exactly 'sat' is
      // 'orbit'. Drives the active-view decision + the sat-trail skip below.
      viewMode: s.viewMode === 'sat' ? 'sat' : 'orbit',
      categories: {
        debris: c.debris !== false,
        star: c.star !== false,
        comet: c.comet !== false,
        satellite: c.satellite !== false,
        groundHot: c.groundHot !== false,
        unknown: c.unknown !== false,
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

  // Current scene epoch (Unix ms) for frame `frameNow`. On the very first call
  // it anchors the epoch's base to the frame clock; thereafter it advances the
  // stored epoch by the elapsed frame-clock delta, so the Sun (and any epoch
  // consumer) ticks smoothly between the ~1 Hz serverTime snapshots instead of
  // snapping once per second.
  function sceneNowMs(frameNow) {
    if (Number.isNaN(sceneEpochBaseNow)) {
      sceneEpochBaseNow = frameNow;
      return sceneEpochMs;
    }
    return sceneEpochMs + (frameNow - sceneEpochBaseNow);
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
        iconArr[n] = def.icon;
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
    gl.bindBuffer(gl.ARRAY_BUFFER, objIconBuf);
    gl.bufferSubData(gl.ARRAY_BUFFER, 0, iconArr.subarray(0, n));
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

  // Rebuild the celestial background (star positions, planets, Moon) for the
  // current scene epoch. Only POSITIONS/colours that move with time are rebuilt
  // here — star colour/size are static (built at setup). Driven by the raw
  // sceneEpochMs (NOT sceneNowMs): this runs at the ~1 Hz setSnapshot cadence,
  // and every body shares the Sun's single time source so the whole sky stays
  // mutually consistent. Also called once at setup so the sky exists before the
  // first snapshot.
  function rebuildSky() {
    // Stars: unit ECEF directions (already GMST-rotated by sky.js) scaled onto
    // the SKY_DIST shell, then uploaded to the dynamic star position buffer.
    starsEcefInto(sceneEpochMs, starUnit);
    for (let i = 0; i < STAR_COUNT * 3; i++) starPos[i] = starUnit[i] * SKY_DIST;
    gl.bindBuffer(gl.ARRAY_BUFFER, starPosBuf);
    gl.bufferSubData(gl.ARRAY_BUFFER, 0, starPos);

    // Planets: dir*SKY_DIST; colour = characteristic tint scaled by magnitude;
    // size by magnitude (larger than stars). planetRender feeds overlay labels.
    const ps = planetsEcef(sceneEpochMs);
    planetRender.length = 0;
    let pc = 0;
    for (let i = 0; i < ps.length && pc < PLANET_CAP; i++) {
      const p = ps[i];
      const tint = PLANET_TINT[p.name] || [1, 1, 1];
      const scale = planetTintScale(p.mag);
      const x = p.dir[0] * SKY_DIST, y = p.dir[1] * SKY_DIST, z = p.dir[2] * SKY_DIST;
      planetPosArr[pc * 3] = x; planetPosArr[pc * 3 + 1] = y; planetPosArr[pc * 3 + 2] = z;
      planetColArr[pc * 3] = tint[0] * scale;
      planetColArr[pc * 3 + 1] = tint[1] * scale;
      planetColArr[pc * 3 + 2] = tint[2] * scale;
      planetSizeArr[pc] = planetSizeForMag(p.mag);
      planetRender.push({ name: p.name, x, y, z });
      pc++;
    }
    planetCount = pc;
    gl.bindBuffer(gl.ARRAY_BUFFER, planetPosBuf);
    gl.bufferSubData(gl.ARRAY_BUFFER, 0, planetPosArr.subarray(0, pc * 3));
    gl.bindBuffer(gl.ARRAY_BUFFER, planetColBuf);
    gl.bufferSubData(gl.ARRAY_BUFFER, 0, planetColArr.subarray(0, pc * 3));
    gl.bindBuffer(gl.ARRAY_BUFFER, planetSizeBuf);
    gl.bufferSubData(gl.ARRAY_BUFFER, 0, planetSizeArr.subarray(0, pc));

    // Moon: unit ECEF direction scaled onto the shell, plus the illuminated
    // fraction (used only in the label). Drawn via the marker soft-disc path.
    const md = moonDirectionEcef(sceneEpochMs);
    moonRender = {
      x: md[0] * SKY_DIST,
      y: md[1] * SKY_DIST,
      z: md[2] * SKY_DIST,
      fraction: moonIlluminatedFraction(sceneEpochMs),
    };
  }

  function appendTrails(nowMs) {
    // id -> cat lookup for this snapshot's objects, built once per call and
    // used below both to stamp server-fed trail cat and (unchanged) by the
    // client-built object loop.
    const catById = new Map();
    if (snapshot && Array.isArray(snapshot.objects)) {
      for (const o of snapshot.objects) catById.set(o.id, o.cat);
    }

    // Server-fed trails (OLV2 `trailPoints`, docs/PROTOCOL_WS.md): each
    // sample's `t` (UTC epoch seconds) is mapped onto the renderer's own
    // trail clock via the scene's serverTime anchor, landing it alongside
    // the client-built samples appended below (both keyed in nowMs terms).
    // Skipped entirely when serverTime is missing/unparseable — there is
    // nothing to anchor the mapping against.
    const serverTimeMs = snapshot && snapshot.serverTime ? Date.parse(snapshot.serverTime) : NaN;
    if (snapshot && Array.isArray(snapshot.trailPoints) && Number.isFinite(serverTimeMs)) {
      for (const p of snapshot.trailPoints) {
        let t = trails.get(p.id);
        // Prefer this snapshot's object row for cat (a track just reported
        // may have changed type); otherwise keep the trail's existing cat.
        const cat = catById.has(p.id) ? catById.get(p.id) : (t ? t.cat : undefined);
        if (!CAT[cat]) continue; // unknown to CAT, same as the object loop below
        const tMs = nowMs - (serverTimeMs - p.t * 1000);
        if (t && t.s.length && tMs <= t.s[t.s.length - 1].t) continue; // defensive monotonicity
        if (!t) { t = { cat, s: [], server: true }; trails.set(p.id, t); }
        t.cat = cat;
        t.server = true;
        const w = worldFromMeters(p.pos);
        t.s.push({ t: tMs, x: w[0], y: w[1], z: w[2] });
        if (t.s.length > SERVER_TRAIL_CAP) t.s.shift();
      }
    }

    if (snapshot && Array.isArray(snapshot.objects)) {
      for (const o of snapshot.objects) {
        if (!CAT[o.cat]) continue;
        const existing = trails.get(o.id);
        // Server-fed trail: the 1 Hz snapshot position would duplicate/
        // zig-zag against the higher-rate trailPoints samples above.
        if (existing && existing.server) continue;
        const w = worldFromMeters(o.pos);
        let t = existing;
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
    // In sat mode the camera IS the satellite, so its own trail is meaningless
    // (it would smear across the whole frame): skip the 'sat:<id>' key. setSettings
    // rebuilds on every viewMode change, so this stays consistent with the mode.
    const hideSatTrail = settings.viewMode === 'sat';

    // First pass: count qualifying segments (younger than trailSeconds, non-degenerate).
    let segCount = 0;
    for (const [k, t] of trails) {
      if (hideSatTrail && typeof k === 'string' && k.indexOf('sat:') === 0) continue;
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
    for (const [key, t] of trails) {
      if (hideSatTrail && typeof key === 'string' && key.indexOf('sat:') === 0) continue;
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

  // Feed the active view's transform uniforms to whichever program is bound.
  // uFisheye must be set on every program each frame (uniforms are per-program
  // state and the programs are switched between draws). Orbit sets uMVP and is
  // byte-for-byte the pre-sat path; sat sets the four fisheye uniforms and lets
  // the shader ignore uMVP. `u` is any program's uniform-location bundle above.
  function setTransformUniforms(u) {
    gl.uniform1i(u.fisheye, activeView.fisheye ? 1 : 0);
    if (activeView.fisheye) {
      gl.uniformMatrix4fv(u.view, false, activeView.view);
      gl.uniform1f(u.thetaMax, SAT_THETA_MAX);
      gl.uniform1f(u.aspect, activeView.aspectGl);
      gl.uniform2f(u.depthRange, SAT_NEAR, SAT_FAR);
    } else {
      gl.uniformMatrix4fv(u.mvp, false, activeView.mvp);
    }
  }

  function drawEarth(sunDir, eye) {
    gl.useProgram(earthProg);
    disableAttribs();
    setTransformUniforms(earthU);
    gl.uniform3fv(earthU.sun, sunDir);
    gl.uniform1f(earthU.ambient, 0.25);
    gl.uniform3fv(earthU.eye, eye);
    // Poll async texture readiness each frame; false -> procedural fallback path.
    gl.uniform1i(earthU.hasDay, earthTex.day ? 1 : 0);
    gl.uniform1i(earthU.hasNight, earthTex.night ? 1 : 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, earthPosBuf);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, earthNrmBuf);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, earthUvBuf);
    gl.enableVertexAttribArray(2);
    gl.vertexAttribPointer(2, 2, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, earthIdxBuf);
    gl.disable(gl.BLEND);
    gl.depthMask(true);
    gl.drawElements(gl.TRIANGLES, earthIndexCount, gl.UNSIGNED_SHORT, 0);
  }

  function drawLines(buffer, vertCount) {
    if (vertCount <= 0) return;
    gl.useProgram(lineProg);
    disableAttribs();
    setTransformUniforms(lineU);
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 28, 0);  // pos: 3 floats
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 4, gl.FLOAT, false, 28, 12); // rgba: 4 floats @ 12 bytes
    gl.enable(gl.BLEND);
    gl.depthMask(false);
    gl.drawArrays(gl.LINES, 0, vertCount);
  }

  // Draw an arbitrary textured-sprite point set through pointsProg. This is the
  // generalized body of the object draw: the tracked objects and the sky's
  // stars/planets are all skyDot/icon GL_POINTS, so they share this program
  // verbatim — no new GLSL, and the fisheye branch is applied in sat mode
  // automatically by setTransformUniforms(). Depth-tested with depthMask(false)
  // so nearer geometry (Sun, tracked objects) correctly paints over them.
  function drawPointset(posBuf, colBuf, sizeBuf, iconBuf, count) {
    if (count <= 0) return;
    gl.useProgram(pointsProg);
    disableAttribs();
    setTransformUniforms(pointsU);
    gl.uniform1f(pointsU.dpr, dpr);
    gl.bindBuffer(gl.ARRAY_BUFFER, posBuf);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, colBuf);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, sizeBuf);
    gl.enableVertexAttribArray(2);
    gl.vertexAttribPointer(2, 1, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, iconBuf);
    gl.enableVertexAttribArray(3);
    gl.vertexAttribPointer(3, 1, gl.FLOAT, false, 0, 0);
    gl.enable(gl.BLEND);
    gl.depthMask(false);
    gl.drawArrays(gl.POINTS, 0, count);
  }

  function drawPoints() {
    drawPointset(objPosBuf, objColBuf, objSizeBuf, objIconBuf, objCount);
  }

  function drawMarker(worldPos, sizePx, color, useAtlas) {
    gl.useProgram(markerProg);
    disableAttribs();
    setTransformUniforms(markerU);
    gl.uniform3fv(markerU.pos, worldPos);
    gl.uniform1f(markerU.size, Math.min(sizePx * dpr, maxPointSize)); // clamp to GPU cap
    gl.uniform3fv(markerU.color, color);
    gl.uniform1i(markerU.useAtlas, useAtlas ? 1 : 0);
    if (useAtlas) {
      // satMarker cell rect (image space); the atlas is bound on unit 2 in frame().
      gl.uniform2f(markerU.uv0, satUV.u0, satUV.v0);
      gl.uniform2f(markerU.uv1, satUV.u1, satUV.v1);
    }
    gl.bindBuffer(gl.ARRAY_BUFFER, markerBuf);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 1, gl.FLOAT, false, 0, 0);
    gl.enable(gl.BLEND);
    gl.depthMask(false);
    gl.drawArrays(gl.POINTS, 0, 1);
  }

  // Project a world point to CSS-pixel screen coordinates using the active view.
  // Orbit: the perspective MVP + w-divide (returns null behind the camera). Sat:
  // the equidistant-fisheye forward map — CPU mirror of FISHEYE_GLSL via
  // sat_camera.js fisheyeProjectNdc() (aspect = cssW/cssH, matching the GL
  // aspectGl ratio) — returning null when the helper does (point at/behind the
  // eye). The NDC->CSS mapping is identical in both branches; |ndc|>1 points
  // still return coordinates (just off-screen), as callers already tolerate.
  function projectToScreen(x, y, z) {
    if (activeView.fisheye) {
      const p = fisheyeProjectNdc(activeView.view, [x, y, z], activeView.aspectCss);
      if (!p) return null;
      return {
        sx: (p.x * 0.5 + 0.5) * cssW,
        sy: (1 - (p.y * 0.5 + 0.5)) * cssH,
      };
    }
    const c = transform(activeView.mvp, [x, y, z, 1]);
    if (c[3] <= 1e-6) return null;
    const nx = c[0] / c[3];
    const ny = c[1] / c[3];
    return {
      sx: (nx * 0.5 + 0.5) * cssW,
      sy: (1 - (ny * 0.5 + 0.5)) * cssH,
    };
  }

  const behindEarth = (eye, o) => occludedBySphere(eye, o.x, o.y, o.z, OCCLUDE_R);

  function drawOverlay(eye, sunPos) {
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0); // work in CSS px, clear device px
    ctx.clearRect(0, 0, cssW, cssH);
    ctx.textBaseline = 'middle';

    // In sat mode the satellite IS the camera, so its ring/label are hidden
    // (per §5.3): its world point sits at the eye where the fisheye map is
    // degenerate and would place a spurious mark near frame centre.
    const hideThisSat = (t) => activeView.fisheye && t === satRender;

    // Selection ring.
    if (selectedId != null) {
      const t = findRenderById(selectedId);
      if (t && !hideThisSat(t)) {
        const p = projectToScreen(t.x, t.y, t.z);
        if (p) {
          ctx.beginPath();
          ctx.strokeStyle = '#ffd54a';
          ctx.lineWidth = 1.5;
          ctx.arc(p.sx, p.sy, 10, 0, Math.PI * 2);
          ctx.stroke();
        }
      }
    }

    // Labels: nearest 200 visible objects by camera distance + satellite +
    // selection. Objects behind the Earth are skipped — the overlay canvas has
    // no depth buffer, so without this their ids draw on top of the globe.
    if (settings.showLabels) {
      ctx.font = '11px monospace';
      ctx.fillStyle = 'rgba(220,228,238,0.92)';
      let list = renderObjects.filter((o) => !behindEarth(eye, o));
      if (list.length > 200) {
        const withD = list.map((o) => ({
          o,
          d: (o.x - eye[0]) ** 2 + (o.y - eye[1]) ** 2 + (o.z - eye[2]) ** 2,
        }));
        withD.sort((p, q) => p.d - q.d);
        list = [];
        for (let i = 0; i < 200; i++) list.push(withD[i].o);
      }
      for (const o of list) {
        const p = projectToScreen(o.x, o.y, o.z);
        if (p) ctx.fillText(String(o.id), p.sx + 6, p.sy);
      }
      if (satRender && !activeView.fisheye && !behindEarth(eye, satRender)) {
        const p = projectToScreen(satRender.x, satRender.y, satRender.z);
        if (p) {
          ctx.fillStyle = '#ffd54a';
          ctx.fillText(String(satRender.id), p.sx + 6, p.sy);
          ctx.fillStyle = 'rgba(220,228,238,0.92)';
        }
      }
      if (selectedId != null) {
        const t = findRenderById(selectedId);
        if (t && !hideThisSat(t)) {
          const p = projectToScreen(t.x, t.y, t.z);
          if (p) ctx.fillText(String(t.id), p.sx + 6, p.sy);
        }
      }
    }

    const onScreen = (p) =>
      p && p.sx > -20 && p.sx < cssW + 20 && p.sy > -20 && p.sy < cssH + 20;

    // Off-screen locator: the Sun and Moon are single points on the sky, easily
    // lost in the 45-deg FOV, so when one is out of frame (or behind the
    // camera, where projectToScreen is invalid) we pin a labelled chevron to
    // the viewport edge pointing toward it. The bearing comes from the body's
    // VIEW-space (x,y) via the active view matrix — valid in both the orbit and
    // sat-fisheye modes, and even when the body is behind the eye. No-op when
    // the body already projects inside the viewport (its disc/label show it).
    const drawBodyLocator = (world, label, color) => {
      const p = projectToScreen(world[0], world[1], world[2]);
      if (p && p.sx >= 0 && p.sx <= cssW && p.sy >= 0 && p.sy <= cssH) return;
      const v = transform(activeView.view, [world[0], world[1], world[2], 1]);
      let dx = v[0];
      let dy = -v[1]; // view +y is up; canvas +y is down
      const dl = Math.hypot(dx, dy);
      if (dl < 1e-6) { dx = 0; dy = 1; } else { dx /= dl; dy /= dl; }
      const cx = cssW / 2;
      const cy = cssH / 2;
      const margin = 30;
      const s = Math.min(
        dx !== 0 ? (cx - margin) / Math.abs(dx) : Infinity,
        dy !== 0 ? (cy - margin) / Math.abs(dy) : Infinity,
      );
      const ex = cx + dx * s;
      const ey = cy + dy * s;
      ctx.save();
      ctx.translate(ex, ey);
      ctx.rotate(Math.atan2(dy, dx));
      ctx.fillStyle = color;
      ctx.beginPath();
      ctx.moveTo(7, 0);
      ctx.lineTo(-4, -5);
      ctx.lineTo(-4, 5);
      ctx.closePath();
      ctx.fill();
      ctx.restore();
      ctx.font = '11px monospace';
      ctx.fillStyle = color;
      ctx.textAlign = 'center';
      ctx.fillText(label, cx + dx * (s - 20), cy + dy * (s - 20));
      ctx.textAlign = 'start';
    };

    // Sun (always, independent of the sky toggle): label near the disc when
    // on-screen, else an edge locator.
    const sp = projectToScreen(sunPos[0], sunPos[1], sunPos[2]);
    if (onScreen(sp)) {
      ctx.font = '11px monospace';
      ctx.fillStyle = 'rgba(255,235,180,0.95)';
      ctx.fillText('Sun ●', sp.sx + 8, sp.sy);
    } else {
      drawBodyLocator(sunPos, 'Sun', 'rgba(255,220,140,0.95)');
    }

    // Sky labels (docs/FEATURE_SKY.md §8.3): subtle, muted names for the Moon
    // (with its illuminated fraction) and each on-screen planet. The Moon also
    // gets an edge locator when off-screen (it is often on the far side of the
    // sky from the planets); planets keep on-screen-only labels.
    if (settings.showSky) {
      ctx.font = '11px monospace';
      ctx.fillStyle = 'rgba(210,214,224,0.80)';
      if (moonRender) {
        const mp = projectToScreen(moonRender.x, moonRender.y, moonRender.z);
        const moonLabel = `Moon ${Math.round(moonRender.fraction * 100)}%`;
        if (onScreen(mp)) {
          ctx.fillText(moonLabel, mp.sx + 8, mp.sy);
        } else {
          drawBodyLocator([moonRender.x, moonRender.y, moonRender.z], moonLabel, 'rgba(214,214,208,0.95)');
        }
      }
      for (const pr of planetRender) {
        const pp = projectToScreen(pr.x, pr.y, pr.z);
        if (onScreen(pp)) {
          ctx.fillText(pr.name.charAt(0).toUpperCase() + pr.name.slice(1), pp.sx + 8, pp.sy);
        }
      }
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
    // Anchor the scene epoch to the state message's serverTime (docs/
    // FEATURE_SKY.md §8.1) when present and parseable. This is what fixes the
    // latent Sun bug — the renderer used to feed a since-page-load clock into
    // sunDirectionEcef(), freezing the Sun near 1970. sceneNowMs() advances
    // this epoch smoothly between snapshots; if serverTime is absent/invalid
    // the previous epoch (default Date.now()) is kept.
    if (snap && snap.serverTime && Number.isFinite(Date.parse(snap.serverTime))) {
      sceneEpochMs = Date.parse(snap.serverTime);
      sceneEpochBaseNow = lastSnapNow;
    }
    appendTrails(lastSnapNow);
    rebuildObjectBuffer();
    rebuildSatellite();
    rebuildTrailBuffer();
    rebuildSky();
  }

  function setSettings(s) {
    settings = normalizeSettings(s);
    rebuildObjectBuffer();
    rebuildTrailBuffer();
  }

  function setSelected(idOrNull) {
    selectedId = idOrNull == null ? null : idOrNull;
  }

  // Resolve the active view for this frame()/pick(). Sat mode wins only when it
  // is requested AND a satellite position exists AND sat_camera can build a view
  // matrix (|pos| >= 1e-9); any failure falls back to the orbit camera for that
  // call — deterministic, no throw (§5.3). Kept flat: no per-object CPU matrix
  // work (the shaders do the fisheye transform on the GPU). aspectGl (GL uniform)
  // and aspectCss (projectToScreen) are identical ratios of the same canvas.
  function computeActiveView() {
    if (settings.viewMode === 'sat' && snapshot && snapshot.satellite && snapshot.satellite.pos) {
      const s = snapshot.satellite;
      const satPos = worldFromMeters(s.pos);            // same worldFromMeters() as satRender
      const view = satViewMatrix(satPos, s.vel || null); // only the vel direction is used
      if (view) {
        return {
          fisheye: true,
          view,
          mvp: null,
          eye: satPos,                                   // camera sits at the satellite
          aspectGl: glCanvas.width / glCanvas.height,
          aspectCss: cssW / cssH,
        };
      }
    }
    const orbitView = camera.getViewMatrix();
    return {
      fisheye: false,
      view: orbitView,
      mvp: multiply(proj, orbitView),
      eye: camera.getEye(),
      aspectGl: glCanvas.width / glCanvas.height,
      aspectCss: cssW / cssH,
    };
  }

  function frame(nowMs) {
    const now = typeof nowMs === 'number' ? nowMs : Date.now();
    activeView = computeActiveView();
    const eye = activeView.eye;
    // Sun from the scene epoch (docs/FEATURE_SKY.md §8.1), NOT the raw frame
    // clock: sceneNowMs() maps performance.now() into serverTime-based wall
    // time so the Sun sits at the real subsolar point and stays consistent with
    // the Moon/planets/stars (which share the same epoch via rebuildSky()).
    const sceneMs = sceneNowMs(now);
    const sunDir = normalize(sunDirectionEcef(sceneMs));
    const sunPos = [sunDir[0] * SUN_DIST, sunDir[1] * SUN_DIST, sunDir[2] * SUN_DIST];

    gl.viewport(0, 0, glCanvas.width, glCanvas.height);
    gl.clearColor(0x05 / 255, 0x07 / 255, 0x0d / 255, 1); // near-black #05070d
    gl.enable(gl.DEPTH_TEST);
    gl.depthFunc(gl.LEQUAL);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    gl.depthMask(true);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);

    // Bind textures to their fixed units. Earth day/night stay null until
    // textures.js finishes loading; drawEarth polls their readiness. Rebinding
    // each frame is cheap and keeps state robust against the async uploads that
    // textures.js performs between frames on whatever unit is active. While a
    // texture is still loading, the always-ready atlas is bound as a placeholder:
    // the shader never samples that unit (uHasDay/uHasNight gate it), but leaving
    // an incomplete (null) texture on a statically-referenced sampler makes
    // browsers log per-draw "incomplete texture" warnings during the load window.
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, earthTex.day || atlasTex);
    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, earthTex.night || atlasTex);
    gl.activeTexture(gl.TEXTURE2);
    gl.bindTexture(gl.TEXTURE_2D, atlasTex);
    gl.activeTexture(gl.TEXTURE0);

    drawEarth(sunDir, eye);
    drawLines(gratBuf, gratVertCount);
    // Celestial background (docs/FEATURE_SKY.md §8.2): stars, then the Moon,
    // then planets — drawn AFTER the Earth + graticule but BEFORE the Sun and
    // tracked objects, so nearer bodies paint over the sky. Every draw here is
    // depth-tested with depthMask(false): the Earth's limb (drawn first, with
    // depth writes) correctly occludes bodies behind it, and the objects below
    // still paint over the sky. Reuses setTransformUniforms() so the fisheye
    // path applies in sat mode too. Whole block gated on settings.showSky.
    if (settings.showSky) {
      drawPointset(starPosBuf, starColBuf, starSizeBuf, starIconBuf, STAR_COUNT);
      if (moonRender) {
        drawMarker([moonRender.x, moonRender.y, moonRender.z], MOON_SIZE_PX, MOON_COLOR, false);
      }
      if (planetCount > 0) {
        drawPointset(planetPosBuf, planetColBuf, planetSizeBuf, planetIconBuf, planetCount);
      }
    }
    drawMarker(sunPos, 42, SUN_COLOR, false);               // Sun: soft procedural disc (both modes)
    if (settings.showTrails) drawLines(trailBuf, trailVertCount);
    drawPoints();
    // Sat mode hides the satellite marker + velocity vector: the camera IS the
    // satellite (§5.3). Its trail is already skipped in rebuildTrailBuffer.
    if (!activeView.fisheye) {
      if (velActive) drawLines(velBuf, 2);
      if (satRender) drawMarker([satRender.x, satRender.y, satRender.z], 30, SAT_COLOR, true); // sat art
    }

    drawOverlay(eye, sunPos);
  }

  function pick(x, y) {
    activeView = computeActiveView();      // same active-view decision as frame()
    const eye = activeView.eye;
    let bestId = null;
    let bestD = Infinity;
    let bestCam = Infinity;
    const consider = (o) => {
      if (behindEarth(eye, o)) return; // can't click through the globe
      const p = projectToScreen(o.x, o.y, o.z);
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
    // Sat mode excludes the satellite (§5.3): it is the camera, not a target.
    if (!activeView.fisheye && satRender) consider(satRender);
    for (let i = 0; i < renderObjects.length; i++) consider(renderObjects[i]);
    return bestId;
  }

  function dispose() {
    camera.dispose();
  }

  // Build the sky once up front (epoch defaults to Date.now()) so stars, Moon
  // and planets are present from the very first frame(), before any snapshot.
  rebuildSky();

  return { resize, setSnapshot, setSettings, setSelected, frame, pick, dispose };
}
