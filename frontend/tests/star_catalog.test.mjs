// Unit tests for star_catalog.js (built-in bright-star data module).
// Run: node --test frontend/tests/star_catalog.test.mjs
import test from 'node:test';
import assert from 'node:assert/strict';
import { STAR_CATALOG } from '../js/star_catalog.js';

/** Great-circle angular separation (degrees) between two ra/dec points (degrees). */
function angularSepDeg(ra1, dec1, ra2, dec2) {
  const D = Math.PI / 180;
  const p1 = dec1 * D, p2 = dec2 * D;
  const dRa = (ra1 - ra2) * D;
  const cosSep = Math.sin(p1) * Math.sin(p2) + Math.cos(p1) * Math.cos(p2) * Math.cos(dRa);
  const clamped = Math.min(1, Math.max(-1, cosSep));
  return Math.acos(clamped) / D;
}

test('module import touches no DOM/globals (import-safe)', () => {
  // If import time had any side effect requiring a DOM, importing this file
  // (which imports star_catalog.js at the top) would already have thrown
  // under plain node. Just assert the exported surface is present.
  assert.ok(Array.isArray(STAR_CATALOG));
});

test('catalog has at least 150 entries', () => {
  assert.ok(
    STAR_CATALOG.length >= 150,
    `expected >= 150 stars, got ${STAR_CATALOG.length}`,
  );
});

test('every entry has finite ra/dec/mag within range', () => {
  for (const [i, star] of STAR_CATALOG.entries()) {
    assert.equal(typeof star.ra, 'number', `entry ${i} ra should be a number`);
    assert.ok(Number.isFinite(star.ra), `entry ${i} ra should be finite`);
    assert.ok(star.ra >= 0 && star.ra < 360, `entry ${i} ra ${star.ra} out of [0,360)`);

    assert.equal(typeof star.dec, 'number', `entry ${i} dec should be a number`);
    assert.ok(Number.isFinite(star.dec), `entry ${i} dec should be finite`);
    assert.ok(star.dec >= -90 && star.dec <= 90, `entry ${i} dec ${star.dec} out of [-90,90]`);

    assert.equal(typeof star.mag, 'number', `entry ${i} mag should be a number`);
    assert.ok(Number.isFinite(star.mag), `entry ${i} mag should be finite`);
  }
});

test('all magnitudes are within bright-star sanity bounds (<= ~4.0)', () => {
  for (const [i, star] of STAR_CATALOG.entries()) {
    assert.ok(star.mag <= 4.0, `entry ${i} (${star.name ?? 'unnamed'}) mag ${star.mag} exceeds 4.0`);
  }
});

test('names, where present, are non-empty strings', () => {
  for (const [i, star] of STAR_CATALOG.entries()) {
    if ('name' in star) {
      assert.equal(typeof star.name, 'string', `entry ${i} name should be a string`);
      assert.ok(star.name.length > 0, `entry ${i} name should be non-empty`);
    }
  }
});

test('at least 30 entries carry a name (the required brightest subset)', () => {
  const named = STAR_CATALOG.filter((s) => typeof s.name === 'string' && s.name.length > 0);
  assert.ok(named.length >= 30, `expected >= 30 named stars, got ${named.length}`);
});

test('spot-check: Sirius is present near ra 101.29, dec -16.72 with mag < -1', () => {
  const hit = STAR_CATALOG.find(
    (s) => angularSepDeg(s.ra, s.dec, 101.29, -16.72) <= 1.0,
  );
  assert.ok(hit, 'no catalog entry found within 1 degree of Sirius');
  assert.ok(hit.mag < -1, `expected Sirius-like mag < -1, got ${hit.mag}`);
});

test('spot-check: Vega is present near ra 279.23, dec 38.78', () => {
  const hit = STAR_CATALOG.find(
    (s) => angularSepDeg(s.ra, s.dec, 279.23, 38.78) <= 1.0,
  );
  assert.ok(hit, 'no catalog entry found within 1 degree of Vega');
  assert.ok(hit.mag < 1, `expected Vega-like mag < 1, got ${hit.mag}`);
});

test('spot-check: first-magnitude stars land within 0.5 degrees of their known positions', () => {
  const knownBright = [
    { ra: 101.29, dec: -16.72, name: 'Sirius' },
    { ra: 95.99, dec: -52.70, name: 'Canopus' },
    { ra: 279.23, dec: 38.78, name: 'Vega' },
    { ra: 88.79, dec: 7.41, name: 'Betelgeuse' },
    { ra: 78.63, dec: -8.20, name: 'Rigel' },
    { ra: 213.92, dec: 19.18, name: 'Arcturus' },
    { ra: 79.17, dec: 45.998, name: 'Capella' },
  ];
  for (const target of knownBright) {
    const hit = STAR_CATALOG.find(
      (s) => angularSepDeg(s.ra, s.dec, target.ra, target.dec) <= 0.5,
    );
    assert.ok(hit, `no catalog entry within 0.5 degrees of ${target.name}`);
  }
});
