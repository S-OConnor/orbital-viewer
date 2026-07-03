// Unit tests for sky.js (low-precision ephemerides: GMST, Moon, planets, stars).
// Run: node --test frontend/tests/sky.test.mjs
import test from 'node:test';
import assert from 'node:assert/strict';
import {
  gmstRad,
  raDecToEci,
  eciToEcef,
  moonDirectionEcef,
  moonIlluminatedFraction,
  PLANET_NAMES,
  planetsEcef,
  STAR_COUNT,
  STAR_MAGS,
  starsEcefInto,
} from '../js/sky.js';
import { STAR_CATALOG } from '../js/star_catalog.js';

const DEG = Math.PI / 180;
const TWO_PI = 2 * Math.PI;
const len = (v) => Math.hypot(v[0], v[1], v[2]);

/** Wrap an angle in radians to (-pi, pi]. */
function wrapToPi(a) {
  let r = ((a + Math.PI) % TWO_PI);
  if (r < 0) r += TWO_PI;
  return r - Math.PI;
}

const SAMPLE_DATES = [
  Date.UTC(2026, 0, 1, 0, 0, 0),
  Date.UTC(2026, 3, 15, 6, 30, 0),
  Date.UTC(2026, 6, 2, 12, 0, 0),
  Date.UTC(2026, 9, 20, 18, 45, 0),
  Date.UTC(2027, 1, 9, 3, 15, 0),
];

test('gmstRad is in [0, 2*pi) and matches the J2000 value', () => {
  for (const d of SAMPLE_DATES) {
    const g = gmstRad(d);
    assert.ok(g >= 0 && g < TWO_PI, `gmst out of range: ${g}`);
  }
  // At J2000.0 (2000-01-01 12:00 UTC) GMST = 280.4606 deg = 4.8949 rad.
  const gJ2000 = gmstRad(Date.UTC(2000, 0, 1, 12, 0, 0));
  assert.ok(Math.abs(gJ2000 - 4.8949) < 1e-2, `expected ~4.8949, got ${gJ2000}`);
});

test('gmstRad accepts both a Date and a number (Unix ms)', () => {
  const ms = Date.UTC(2026, 6, 2, 12, 0, 0);
  assert.ok(Math.abs(gmstRad(new Date(ms)) - gmstRad(ms)) < 1e-12);
});

test('raDecToEci gives unit vectors and the expected axes', () => {
  assert.ok(Math.abs(len(raDecToEci(1.2, -0.3)) - 1) < 1e-12);
  const ex = raDecToEci(0, 0);
  assert.ok(Math.abs(ex[0] - 1) < 1e-12 && Math.abs(ex[1]) < 1e-12 && Math.abs(ex[2]) < 1e-12);
  const ey = raDecToEci(Math.PI / 2, 0);
  assert.ok(Math.abs(ey[0]) < 1e-12 && Math.abs(ey[1] - 1) < 1e-12 && Math.abs(ey[2]) < 1e-12);
  const ez = raDecToEci(0, Math.PI / 2);
  assert.ok(Math.abs(ez[0]) < 1e-12 && Math.abs(ez[1]) < 1e-12 && Math.abs(ez[2] - 1) < 1e-12);
});

test('eciToEcef preserves length and applies Rz(-gmst)', () => {
  const g = 1.3;
  const v = raDecToEci(0.7, 0.4);
  assert.ok(Math.abs(len(eciToEcef(v, g)) - len(v)) < 1e-12);
  const r = eciToEcef([1, 0, 0], g);
  assert.ok(Math.abs(r[0] - Math.cos(g)) < 1e-12);
  assert.ok(Math.abs(r[1] - -Math.sin(g)) < 1e-12);
  assert.ok(Math.abs(r[2]) < 1e-12);
});

test('moonDirectionEcef is a finite unit vector at several dates', () => {
  for (const d of SAMPLE_DATES) {
    const v = moonDirectionEcef(d);
    assert.equal(v.length, 3);
    assert.ok(v.every(Number.isFinite), `non-finite component: ${v}`);
    assert.ok(Math.abs(len(v) - 1) < 1e-9, `expected unit length, got ${len(v)}`);
  }
});

test('moonIlluminatedFraction is in [0,1] and changes over ~10 days', () => {
  for (const d of SAMPLE_DATES) {
    const f = moonIlluminatedFraction(d);
    assert.ok(Number.isFinite(f) && f >= 0 && f <= 1, `fraction out of range: ${f}`);
  }
  const t0 = SAMPLE_DATES[0];
  const t1 = t0 + 10 * 86400000;
  assert.ok(
    Math.abs(moonIlluminatedFraction(t0) - moonIlluminatedFraction(t1)) > 1e-3,
    'illuminated fraction should change over ~10 days',
  );
});

test('planetsEcef returns the five named planets as unit vectors with finite mag', () => {
  const ps = planetsEcef(SAMPLE_DATES[2]);
  assert.equal(ps.length, 5);
  assert.deepEqual(ps.map((p) => p.name), PLANET_NAMES);
  for (const p of ps) {
    assert.ok(Math.abs(len(p.dir) - 1) < 1e-9, `${p.name} dir not unit: ${len(p.dir)}`);
    assert.ok(p.dir.every(Number.isFinite), `${p.name} has non-finite dir`);
    assert.ok(Number.isFinite(p.mag), `${p.name} mag not finite: ${p.mag}`);
  }
});

test('STAR_COUNT and STAR_MAGS are consistent with the catalog', () => {
  assert.equal(STAR_COUNT, STAR_CATALOG.length);
  assert.equal(STAR_MAGS.length, STAR_COUNT);
  assert.ok(STAR_MAGS instanceof Float32Array);
  for (let i = 0; i < STAR_COUNT; i++) {
    assert.ok(Math.abs(STAR_MAGS[i] - STAR_CATALOG[i].mag) < 1e-4);
  }
});

test('starsEcefInto fills unit vectors and rotates with the clock', () => {
  const t0 = SAMPLE_DATES[0];
  const out = new Float32Array(STAR_COUNT * 3);
  assert.equal(starsEcefInto(t0, out), STAR_COUNT);
  for (let i = 0; i < STAR_COUNT; i++) {
    const b = i * 3;
    const l = Math.hypot(out[b], out[b + 1], out[b + 2]);
    assert.ok(Math.abs(l - 1) < 1e-5, `star ${i} not unit length: ${l}`);
  }
  // A different time must rotate the sky (values change).
  const out2 = new Float32Array(STAR_COUNT * 3);
  starsEcefInto(t0 + 6 * 3600000, out2);
  let changed = false;
  for (let i = 0; i < out.length; i++) {
    if (Math.abs(out[i] - out2[i]) > 1e-4) { changed = true; break; }
  }
  assert.ok(changed, 'star ECEF vectors should change at a different time');
});

test('starsEcefInto does not allocate a new array (writes in place)', () => {
  const out = new Float32Array(STAR_COUNT * 3);
  const ret = starsEcefInto(SAMPLE_DATES[1], out);
  assert.equal(ret, STAR_COUNT);
  // The provided buffer is the one that was filled.
  assert.ok(out.some((x) => x !== 0));
});

test('CONSISTENCY: catalog star 0 ECEF longitude equals RA - GMST', () => {
  const t = SAMPLE_DATES[3];
  const g = gmstRad(t);
  const ra0 = STAR_CATALOG[0].ra * DEG;
  const out = new Float32Array(STAR_COUNT * 3);
  starsEcefInto(t, out);
  const lon = Math.atan2(out[1], out[0]);
  const expected = wrapToPi(ra0 - g);
  assert.ok(
    Math.abs(wrapToPi(lon - expected)) < 1e-4,
    `ECEF longitude ${lon} != RA-GMST ${expected}`,
  );
});
