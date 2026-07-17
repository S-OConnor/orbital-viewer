# Feature: Satellite POV View (v0.3)

Status: **frozen interface spec** — plan-of-record for the sat-view feature.
Written by the lead architect; implementation agents MUST NOT change the
interfaces in §3–§5 without architect sign-off. Companion to docs/PLAN.md
(renderer public API, §6 there, receives an additive extension only).

## 1. Overview

Add a user-toggleable second view mode to the frontend:

* `orbit` (existing, default) — free orbit camera around Earth.
* `sat` (new) — first-person view from the primary satellite, boresight
  fixed at nadir (Earth centre), rendered with an **equidistant fisheye
  projection at 170° vertical FOV** so the Earth disc (≈134° angular
  diameter at the 550 km simulator altitude) is visible WITH surrounding
  space beyond the limb ("really wide camera").

Backend, simulator, and both wire protocols are untouched. Frontend only.
No third-party code. All coordinates below are **scene units**
(1 unit = 1,000 km, ECEF axes, +Z north — same as renderer.js).

## 2. Why fisheye, not perspective

From 550 km altitude the Earth limb sits ~67° off nadir. A perspective
projection cannot reach 134°+ FOV without unbounded edge stretching
(tan 85° ≈ 11.4). The equidistant azimuthal model (screen radius ∝ angle
off boresight) is the standard wide-sensor model, well-conditioned to
180°+ and trivially invertible for picking.

## 3. Frozen module interface — `frontend/js/sat_camera.js` (NEW, Agent B)

Pure logic module, IMPORT-SAFE, no DOM/WebGL access. May import math3.js.

```js
export const SAT_FOV_DEG = 170;   // vertical full FOV of the sat view
export const SAT_NEAR   = 0.05;   // fisheye depth-range near (units)
export const SAT_FAR    = 1000;   // fisheye depth-range far  (units)

/**
 * View matrix for the satellite camera.
 * @param posUnits [x,y,z] satellite position, scene units (ECEF)
 * @param velUnits [x,y,z] satellite velocity (any consistent scale,
 *                 only the direction is used) or null/undefined
 * @returns Float32Array(16) column-major view matrix, or null when
 *          |posUnits| < 1e-9 (renderer then falls back to orbit view).
 */
export function satViewMatrix(posUnits, velUnits)

/**
 * Equidistant fisheye forward projection (CPU mirror of the GLSL in §5).
 * @param viewMatrix from satViewMatrix()
 * @param worldPoint [x,y,z] scene units
 * @param aspect     viewport width/height
 * @param fovDeg     defaults to SAT_FOV_DEG
 * @returns {x, y, depth, theta} — x,y in NDC (MAY exceed [-1,1]; caller
 *          clips), depth in [-1,1], theta = angle off boresight (rad) —
 *          or null when the point is unprojectable (at the eye, or
 *          exactly behind: see §4 epsilons).
 */
export function fisheyeProjectNdc(viewMatrix, worldPoint, aspect, fovDeg = SAT_FOV_DEG)
```

## 4. Normative math (both JS and GLSL implement EXACTLY this)

### 4.1 View matrix

```
eye = posUnits
f   = normalize(-posUnits)                    // boresight: nadir
upC = velUnits - (velUnits·f)f                // velocity ⊥ boresight
if velUnits is null/zero or |upC| < 1e-6:
    upC = [0,0,1] - ([0,0,1]·f)f              // fallback 1: north
if |upC| < 1e-6:                              // sat on the polar axis
    upC = [1,0,0] - ([1,0,0]·f)f              // fallback 2: +X
up  = normalize(upC)
view = lookAt(eye, [0,0,0], up)               // math3.js lookAt
```

Camera looks down −Z in view space; screen-up is the orbital
velocity direction (fallbacks deterministic as above).

### 4.2 Fisheye projection of view-space point `v`

```
thetaMax = (fovDeg/2) * π/180                 // 85° for the default
rho   = |v|            ; if rho < 1e-9        → unprojectable
dir   = v / rho
theta = acos(clamp(-dir.z, -1, 1))            // 0 = boresight … π = behind
depth = clamp((rho - SAT_NEAR)/(SAT_FAR - SAT_NEAR), 0, 1) * 2 - 1
pLen  = |dir.xy|
if pLen < 1e-6:
    if theta > π/2                             → unprojectable (behind)
    else                                       → x = 0, y = 0 (boresight)
else:
    r = theta / thetaMax
    x = r * (dir.x / pLen) / aspect
    y = r * (dir.y / pLen)
```

Properties (test fixtures derive from these): boresight → (0,0);
theta == thetaMax at aspect 1 → radius 1 in NDC; radius grows linearly
with theta; depth strictly increases with rho; points behind the camera
but off-axis project at r > 1 and are clipped by the viewport (this is
correct fisheye behaviour, not an error).

Screen mapping (renderer-side, mirrors the perspective path):
`sx = (x*0.5+0.5)*cssW`, `sy = (1-(y*0.5+0.5))*cssH`. JS uses
`aspect = cssW/cssH`; GL uses drawingBuffer w/h — identical ratios.

## 5. Renderer contract — `frontend/js/renderer.js` (Agent A)

### 5.1 Settings extension (additive to the frozen PLAN.md §6 API)

`setSettings(s)` accepts a new key `viewMode: 'orbit' | 'sat'`.
`normalizeSettings` maps anything that is not exactly `'sat'` to
`'orbit'`. No new public methods.

### 5.2 GLSL

Each of the four vertex shaders (EARTH_VS, LINE_VS, POINTS_VS, MARKER_VS)
gains a fisheye path selected by `uniform bool uFisheye`, with uniforms
`uView` (mat4, view matrix from sat_camera.js), `uThetaMax`, `uAspect`,
`uDepthRange` (vec2 = [SAT_NEAR, SAT_FAR]). The shared GLSL function is
verbatim (LOCKSTEP with sat_camera.js §4.2 — change both together):

```glsl
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
}
```

Orbit path stays byte-for-byte the existing `uMVP` transform. Known,
accepted approximations in sat mode: w=1 makes varying interpolation
screen-linear (Earth mesh is 64×96 — dense enough); line segments that
cross far behind the camera bend toward the outer ring as real fisheye
footage does.

### 5.3 Behaviour matrix in `sat` mode

| Element                    | orbit mode | sat mode |
|----------------------------|-----------|----------|
| Earth, graticule, Sun disc | drawn     | drawn (fisheye) |
| Tracked objects + trails   | drawn     | drawn (fisheye) |
| Satellite marker           | drawn     | **hidden** (camera is there) |
| Velocity vector line       | drawn     | **hidden** |
| Satellite trail (`sat:` key) | drawn   | **hidden** (skip in rebuildTrailBuffer) |
| Satellite label / ring     | drawn     | **hidden**; pick() must not return the satellite id |
| Orbit-camera pointer input | active    | ignored (listeners stay attached) |

Fallback: in `sat` mode with no snapshot or `snapshot.satellite == null`
(or satViewMatrix returns null), render the **orbit** view for that frame.
Deterministic, no throw.

`frame()`, `pick()`, and the overlay (`projectToScreen`) all switch on the
same active-view decision; picking uses `fisheyeProjectNdc` with the same
12-px CSS radius. View-mode changes trigger the usual buffer rebuilds
(`setSettings` already does).

## 6. Settings store — `frontend/js/settings.js` (Agent C)

* `DEFAULTS.viewMode = 'orbit'`.
* `applyPatch` accepts `viewMode` only when it is exactly the string
  `'orbit'` or `'sat'`; anything else leaves the current value.
* Persisted in localStorage under the existing key/versioning; no
  migration needed (missing key → default via existing normalize path).
* site_config.js / config.toml are OUT OF SCOPE for v0.3.

## 7. UI — `frontend/js/ui.js`, `index.html`, `css/style.css`, `main.js` (Agent D)

* Toggle control: a two-state segmented button group in the `.topright`
  cluster, ids **`btnView3d`** and **`btnViewSat`** (labels `3D` / `SAT`,
  active state styled). Clicking writes `settingsStore.update({viewMode})`;
  the store's onChange path (main.js `applyRendererSettings`) carries it to
  the renderer — no direct ui→renderer call.
* Keyboard shortcut **`v`** toggles the mode (ignored while focus is in an
  input/textarea/select or a modal is open).
* Badge: `<div id="viewBadge">` overlaid top-left of the viewport, hidden
  in orbit mode; in sat mode shows `SAT VIEW — nadir, 170° FOV`, or
  `SAT VIEW — awaiting satellite data` when the model has no satellite
  (update in `updateData()`; ui.js reads viewMode from settingsStore).
* main.js: include `viewMode: s.viewMode` in `applyRendererSettings`.
* About modal: one sentence noting the satellite-POV fisheye view.
* No changes to renderer.js/settings.js internals — consume frozen
  interfaces only.

## 8. File ownership (exclusive — no other agent touches these)

| Agent | Files |
|-------|-------|
| A (renderer) | frontend/js/renderer.js |
| B (math)     | frontend/js/sat_camera.js, frontend/tests/sat_camera.test.mjs |
| C (settings) | frontend/js/settings.js, frontend/tests/settings.test.mjs |
| D (UI)       | frontend/js/ui.js, frontend/index.html, frontend/css/style.css, frontend/js/main.js |
| Architect    | docs/*, README.md, frontend/dev/smoke.html, integration |

## 9. Acceptance criteria (verified at integration)

1. `node --test "frontend/tests/*.test.mjs"` passes; new sat_camera tests
   cover: view-matrix orthonormality, eye→origin mapping, nadir on −Z,
   up-vector fallback chain, boresight→(0,0), limb-angle→expected radius,
   linearity in theta, aspect scaling, depth monotonicity, behind/eye
   degeneracy, null-velocity handling.
2. `cmake --build build && ctest --test-dir build` still green (no C++
   changes expected; guard against accidental ones).
3. All modules remain IMPORT-SAFE (importable under Node without DOM).
4. Toggling 3D/SAT at runtime is instant, both directions, with state
   persisted across reload; degraded gracefully when no data flows.
5. Zero third-party code added; style matches existing modules (JSDoc
   headers, comment density, no build step).
