# Feature: Celestial Background — Stars, Moon & Planets (v0.4)

Status: **frozen interface spec** — plan-of-record for the sky feature.
Written by the lead architect; implementation agents MUST NOT change the
interfaces in §3–§8 without architect sign-off. Companion to docs/PLAN.md
(renderer public API, §6 there, receives an additive extension only) and
docs/FEATURE_SATVIEW.md (this feature reuses the existing view pipelines and
must not regress the sat-view fisheye path).

## 1. Overview

Render a time-accurate celestial background behind the Earth and tracked
objects:

* **Stars** — a compact built-in catalog of the ~180 brightest stars,
  placed on a distant celestial shell and rotated into ECEF by Greenwich
  Mean Sidereal Time (GMST) so the sky wheels correctly with the clock.
* **Moon** — a soft grey disc billboard at its true ECEF direction, with an
  illuminated-fraction readout in its label.
* **Planets** — the five naked-eye planets (Mercury, Venus, Mars, Jupiter,
  Saturn) as colored dots sized by apparent magnitude, with name labels.

The epoch driving all of this is the **`serverTime` field of the `state`
message** (ISO-8601 UTC, docs/PROTOCOL_WS.md §2), threaded through the model
into the renderer. The **Sun** is switched onto this same epoch, fixing a
latent bug: the renderer previously fed `performance.now()` (a since-page-load
clock) into `sunDirectionEcef()`, freezing the Sun near the 1970 epoch.

Whole feature is gated by one new setting, **`showSky`** (default **on**).
Backend, simulator, and both wire protocols are untouched (serverTime already
exists on the wire). Frontend only. No third-party code. All coordinates are
**scene units** (1 unit = 1,000 km, ECEF axes, +Z north — same as
renderer.js). All celestial direction vectors are **unit vectors in ECEF**,
identical frame/convention to `sun.js`'s output.

## 2. Design principles (why it is low-risk)

* **No shader changes.** Stars + planets draw through the existing `pointsProg`
  (a single `GL_POINTS` call) using a new soft round "dot" atlas cell; the Moon
  draws through the existing `markerProg` soft-disc path (like the Sun). Because
  both vertex shaders already carry the `uFisheye` sat-view branch, the sky
  renders correctly in BOTH orbit and sat modes with no new GLSL and no risk to
  the docs/FEATURE_SATVIEW.md lockstep.
* **One epoch, one source of time.** The renderer derives a single scene epoch
  from `serverTime` and uses it for the Sun, Moon, planets, and star GMST, so
  every body is mutually consistent.
* **Precision class matches `sun.js`.** Low-precision analytic ephemerides
  (~arcmin–0.5°). This is a visual background, not an almanac.

## 3. Frozen module interface — `frontend/js/star_catalog.js` (NEW, Agent B)

Pure data module, IMPORT-SAFE, no DOM. No logic beyond the literal array.

```js
/**
 * Brightest stars for the celestial background. J2000 equatorial coords.
 * Provenance: positions/magnitudes are astronomical facts derived from
 * public-domain bright-star data (Yale Bright Star Catalogue / Hipparcos);
 * hand-encoded here as first-party data (no third-party code or files).
 * Ordered brightest-first is NOT required.
 *
 * @type {ReadonlyArray<{ra:number, dec:number, mag:number, name?:string}>}
 *   ra   right ascension, DEGREES, [0, 360)
 *   dec  declination,     DEGREES, [-90, 90]
 *   mag  apparent visual magnitude (brighter = smaller/negative; Sirius ≈ -1.46)
 *   name optional common name (only worth setting for the very brightest)
 */
export const STAR_CATALOG = [ /* ~180 entries */ ];
```

Coverage requirement: **all stars of visual magnitude ≤ 3.5** should be
present (~180 stars) so the recognizable constellations read correctly. The
handful of first-magnitude stars must be positioned to within ~0.5°
(e.g. Sirius ra ≈ 101.29, dec ≈ −16.72, mag −1.46; Betelgeuse ra ≈ 88.79,
dec ≈ 7.41, mag 0.42; Vega ra ≈ 279.23, dec ≈ 38.78, mag 0.03).

## 4. Frozen module interface — `frontend/js/sky.js` (NEW, Agent A)

Pure math, IMPORT-SAFE, no DOM/WebGL/globals at import time. May import
`math3.js` and `star_catalog.js`. Mirrors `sun.js` style (JSDoc header,
comment density, `dateOrMs` inputs). Does **not** duplicate the Sun — the
renderer keeps using `sun.js`.

Time input everywhere: `dateOrMs` = a `Date` or Unix time in milliseconds
(same as `sunDirectionEcef`). Use the SAME GMST formula as `sun.js`
(`GMST_deg = 280.46061837 + 360.98564736629 · (JD − 2451545.0)`, wrapped) so
the whole sky shares one sidereal time with the Sun.

```js
/** Greenwich Mean Sidereal Time as an angle in radians, [0, 2π). */
export function gmstRad(dateOrMs)

/** J2000 equatorial (RA/Dec, radians) → inertial (ECI) unit vector. */
export function raDecToEci(raRad, decRad)   // → [x,y,z]

/** Rotate an ECI unit vector into ECEF: Rz(−gmst)·v  (ECEF lon = RA − GMST). */
export function eciToEcef(vEci, gmst)        // → [x,y,z]

/** Approximate unit vector toward the Moon in ECEF (~<0.5° class). */
export function moonDirectionEcef(dateOrMs)  // → [x,y,z] unit

/** Moon illuminated fraction in [0,1] (0 = new, 1 = full). */
export function moonIlluminatedFraction(dateOrMs)  // → number

/** Naked-eye planet names, fixed order. */
export const PLANET_NAMES  // ['mercury','venus','mars','jupiter','saturn']

/**
 * Geocentric apparent positions of the five naked-eye planets in ECEF.
 * @returns Array<{name:string, dir:[number,number,number], mag:number}>
 *   in PLANET_NAMES order; dir is a unit ECEF vector; mag is apparent
 *   visual magnitude (may be rough — used only for on-screen sizing).
 */
export function planetsEcef(dateOrMs)

/** Number of stars in the built-in catalog (== STAR_CATALOG.length). */
export const STAR_COUNT   // number

/** Per-star apparent visual magnitude, index-aligned with the catalog. */
export const STAR_MAGS    // Float32Array(STAR_COUNT)

/**
 * Rotate every catalog star into an ECEF unit vector for time `dateOrMs`,
 * writing 3 components per star into `out` (must be length ≥ STAR_COUNT*3).
 * Computes GMST once, then applies eciToEcef to each precomputed J2000
 * unit vector. Returns STAR_COUNT. No per-call heap allocation.
 */
export function starsEcefInto(dateOrMs, out)   // → number
```

Implementation notes (non-normative): precompute the J2000 ECI unit vectors
from `STAR_CATALOG` once at import. Moon: a low-precision lunar theory
(e.g. Astronomical Almanac / Meeus ch. 47 abridged) → ecliptic lon/lat →
equatorial (obliquity) → ECEF via GMST. Planets: low-precision Keplerian
elements with per-century rates (Standish/Meeus ch. 31–33), heliocentric →
geocentric → equatorial → ECEF; magnitude from the standard distance/phase
formula is welcome but a nominal per-planet base magnitude is acceptable.

## 5. Model contract — `frontend/js/model.js` (Agent D)

`getSnapshot()` gains one field: **`serverTime`** (the `state` message's
`serverTime` string, or `null` before the first `state`).

```js
getSnapshot() // → { satellite, objects, lastDataTime, serverTime }
```

`applyState(msg, nowMs)` stores `msg.serverTime` (already a validated string
from net.js `parseState`). No other public shape changes. Update
`frontend/tests/model.test.mjs` to assert the passthrough (present after a
state, `null` initially).

## 6. Settings contract — `frontend/js/settings.js` (Agent C)

* `DEFAULTS.showSky = true`.
* `applyPatch` coerces it: `if ('showSky' in patch) base.showSky = Boolean(patch.showSky);`
  (placed with the other boolean flags). This makes it, like `showTrails`/
  `showLabels`, a recognized `siteDefaults`/`config.toml` key automatically —
  acceptable and intended.
* Persisted in the existing localStorage key/versioning; missing key →
  default `true` via the existing normalize path (no migration).
* Update `frontend/tests/settings.test.mjs` for the new default and patching.

## 7. Atlas contract — `frontend/js/atlas.js` (Agent E)

Add exactly one cell — a soft round dot for stars/planets — reusing the
existing tint-by-vertex-color scheme (must stay **white, alpha varies** so
the renderer can tint it per body):

* `ICONS` gains **`skyDot: 6`** (cells 7–15 remain empty).
* A `drawSkyDot(ctx, cellPx)` painter: a soft filled disc built from a few
  concentric white circles of decreasing alpha (bright core → transparent
  edge), in the same "white, alpha varies" style as the other icons and
  using only the ctx surface the test recorder already supports (`arc`,
  `fill`, `set:fillStyle`, etc. — no `createRadialGradient`).
* Register it in `PAINTERS` so `paintAtlas` now draws **7** cells.
* Update `frontend/tests/atlas.test.mjs`: `ICONS` deepEqual now includes
  `skyDot: 6`; `DEFINED_INDICES` = `[0..6]`; the "6 cells" counts become 7;
  the "cells 6–15 empty" test becomes "cells 7–15 empty". The white-only
  assertion runs over the category icons (0–4) only — leave it as is.

`skyDot` (index 6) is a valid grid cell, so `iconUV(ICONS.skyDot)` already
works with no change to `iconUV`.

## 8. Renderer contract — `frontend/js/renderer.js` (Agent A→ integrated by Agent F)

Consumes the frozen `sky.js`, `star_catalog.js` (via sky.js), `atlas.js`
(`ICONS.skyDot`), and the `showSky` / `serverTime` additions. **No new public
methods; no GLSL changes; no changes to the sat-view fisheye lockstep.**

### 8.1 Scene epoch (fixes the Sun)

* New renderer state: `sceneEpochMs`, `sceneEpochBaseNow`. Initialize
  `sceneEpochMs` to a real wall-clock value (`Date.now()`), base to 0.
* In `setSnapshot(snap, nowMs)`: if `snap.serverTime` parses to a finite time
  (`Date.parse`), set `sceneEpochMs = parsed` and `sceneEpochBaseNow = lastSnapNow`.
* Helper `sceneNowMs(frameNow) = sceneEpochMs + (frameNow − sceneEpochBaseNow)`
  advances the clock smoothly between 1 Hz snapshots.
* `frame()`: compute the Sun from `sceneNowMs(now)` instead of `now`. (This is
  the only change to existing Sun behavior.)

### 8.2 Sky geometry & drawing

* Constant `SKY_DIST = 300` (units): beyond the 120-unit object shell, well
  within `FAR = 1000`. Stars, Moon and planets are placed at this radius.
* Build **once** (from `STAR_MAGS`): per-star color buffer (white scaled by a
  brightness factor from magnitude — dimmer stars greyer) and size buffer
  (brighter = larger, ~1.2–4.5 CSS px), plus a constant `aIcon = ICONS.skyDot`
  buffer. Rebuild the **positions** each `setSnapshot` via `starsEcefInto(epoch, …)`
  then scale by `SKY_DIST`.
* Planets: rebuild a small (≤5-point) position/color/size buffer each
  `setSnapshot` from `planetsEcef(epoch)`. Suggested tints — Mercury
  `#c9b8a0`, Venus `#f5f3e0`, Mars `#e0663c`, Jupiter `#e3d2a8`, Saturn
  `#e6cf8f` — scaled/sized by magnitude, larger than stars.
* Moon: one `markerProg` soft-disc draw (uUseAtlas 0) at
  `moonDirectionEcef(epoch) · SKY_DIST`, grey-white `#d8d8d0`, ~26 px.
* Draw order in `frame()` — **after Earth + graticule, before tracked
  objects** (so nearer objects paint over the sky), all with
  `depthMask(false)` and depth-test on (Earth occludes; stars behind the limb
  are correctly culled). Reuse `setTransformUniforms(...)` so the fisheye path
  is applied in sat mode. Gate the entire block on `settings.showSky`.
* `normalizeSettings`: add `showSky: s.showSky !== false` (default true).

### 8.3 Overlay labels

In `drawOverlay`, when `settings.showSky` is on, add subtle labels next to the
Moon (`Moon 63%` using `moonIlluminatedFraction`) and each on-screen planet
(capitalized name), matching the existing Sun-label style (small monospace,
muted color). Stars are not labeled. The Sun label stays as-is.

## 9. UI — `frontend/js/ui.js`, `index.html`, `css/style.css`, `main.js` (Agent G)

* **Settings toggle:** a single checkbox `id="settingShowSky"` in the settings
  modal (place it just after "Show labels"), label **"Show sky (stars, Moon,
  planets)"**. Wire it in `ui.js` exactly like `settingShowLabels`:
  `syncSettingsForm()` sets `.checked = s.showSky`; a `change` listener calls
  `settingsStore.update({ showSky: e.target.checked })`.
* **main.js:** include `showSky: s.showSky` in `applyRendererSettings()`'s
  `renderer.setSettings({…})` call.
* **About modal:** one sentence noting the time-accurate stars/Moon/planets
  background (and that it follows the state message's `serverTime`).
* **css:** no new rules expected (reuse `.field-row`); add only if needed.
* Consume frozen names only — no direct ui→renderer calls, no touching
  settings.js/renderer.js internals.

## 10. File ownership (exclusive — no other agent touches these)

| Agent | Model | Files |
|-------|-------|-------|
| A (ephemerides) | Opus  | frontend/js/sky.js, frontend/tests/sky.test.mjs |
| B (catalog)     | Sonnet| frontend/js/star_catalog.js, frontend/tests/star_catalog.test.mjs |
| C (settings)    | Sonnet| frontend/js/settings.js, frontend/tests/settings.test.mjs |
| D (model)       | Sonnet| frontend/js/model.js, frontend/tests/model.test.mjs |
| E (atlas)       | Sonnet| frontend/js/atlas.js, frontend/tests/atlas.test.mjs |
| F (renderer)    | Opus  | frontend/js/renderer.js |
| G (UI)          | Sonnet| frontend/js/ui.js, frontend/index.html, frontend/css/style.css, frontend/js/main.js |
| Architect       |       | docs/*, README.md, frontend/dev/smoke.html, integration |

Dependency waves: **Wave 1** B, C, D, E, G (parallel; leaf changes against
frozen names). **Wave 2** A (needs B). **Wave 3** F (needs A, E and the C/D
additions). Each agent runs ONLY its own test file(s); the architect runs the
full suite at integration.

## 11. Acceptance criteria (verified at integration)

1. `node --test "frontend/tests/*.test.mjs"` passes, including new
   `sky.test.mjs` (GMST at a known epoch; Moon/Sun/planet/star outputs are
   unit vectors; Moon illuminated fraction in [0,1]; `starsEcefInto` fills
   `STAR_COUNT*3` and preserves unit length; a bright star lands near its
   expected ECEF hour angle at a known time) and `star_catalog.test.mjs`
   (ranges, count ≥ ~150, a spot-checked bright star present).
2. `cmake --build build && ctest --test-dir build` still green — **no C++
   changes** (guard against accidental ones).
3. All JS modules remain IMPORT-SAFE (importable under Node without a DOM);
   `renderer.js` imports cleanly at top level.
4. `showSky` toggles the entire sky live, both directions, state persisted
   across reload; default is **on**.
5. Sun, Moon, planets and stars are mutually consistent and driven by
   `serverTime`; with no data flowing the sky still renders (epoch falls back
   to `Date.now()`), and degrades to the prior look when `showSky` is off.
6. Sky renders correctly in BOTH orbit and sat (fisheye) views; the
   docs/FEATURE_SATVIEW.md lockstep GLSL is untouched.
7. Zero third-party code added; style matches existing modules (JSDoc
   headers, comment density, no build step).
```
