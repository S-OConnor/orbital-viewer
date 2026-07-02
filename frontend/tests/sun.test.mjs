// Unit tests for sun.js (approximate solar direction in ECEF).
// Run: node --test frontend/tests/sun.test.mjs
import test from 'node:test';
import assert from 'node:assert/strict';
import { sunDirectionEcef } from '../js/sun.js';

const len = (v) => Math.hypot(v[0], v[1], v[2]);
const subsolarLonDeg = (v) => (Math.atan2(v[1], v[0]) * 180) / Math.PI;

test('output is a unit vector', () => {
  for (const d of [
    Date.UTC(2026, 0, 1, 0, 0, 0),
    Date.UTC(2026, 5, 21, 8, 0, 0),
    Date.UTC(2026, 8, 23, 6, 0, 0),
    Date.UTC(2026, 11, 21, 15, 0, 0),
  ]) {
    const v = sunDirectionEcef(d);
    assert.equal(v.length, 3);
    assert.ok(Math.abs(len(v) - 1) < 1e-9, `expected unit length, got ${len(v)}`);
  }
});

test('near-zero declination at the March equinox', () => {
  // 2026 March equinox is ~2026-03-20T14:46Z; z = sin(declination) ~ 0.
  const v = sunDirectionEcef(new Date('2026-03-20T14:46:00Z'));
  assert.ok(Math.abs(v[2]) < 0.03, `|z| should be < 0.03, got ${v[2]}`);
});

test('positive declination at the June solstice', () => {
  // z = sin(dec); at the June solstice dec ~ +23.4 deg so sin(dec) ~ 0.397.
  const v = sunDirectionEcef(new Date('2026-06-21T08:00:00Z'));
  assert.ok(v[2] > 0.35, `z should be > 0.35, got ${v[2]}`);
});

test('subsolar longitude near 0 deg at 2026-07-01T12:00Z (local noon at Greenwich)', () => {
  const v = sunDirectionEcef(new Date('2026-07-01T12:00:00Z'));
  const lon = subsolarLonDeg(v);
  assert.ok(Math.abs(lon) < 15, `subsolar longitude should be within +/-15 deg of 0, got ${lon}`);
});

test('accepts both a Date and a number (Unix ms)', () => {
  const ms = Date.parse('2026-07-01T12:00:00Z');
  const fromDate = sunDirectionEcef(new Date(ms));
  const fromNumber = sunDirectionEcef(ms);
  assert.equal(fromDate.length, 3);
  assert.equal(fromNumber.length, 3);
  for (let i = 0; i < 3; i++) {
    assert.ok(
      Math.abs(fromDate[i] - fromNumber[i]) < 1e-12,
      `Date and number inputs should agree on component ${i}`,
    );
  }
});
