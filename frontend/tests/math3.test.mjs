// Unit tests for math3.js occludedBySphere (overlay label culling).
// Run: node --test frontend/tests/math3.test.mjs
import test from 'node:test';
import assert from 'node:assert/strict';
import { occludedBySphere } from '../js/math3.js';

const R = 6.371;
const eye = [0, 0, 30];

test('point on the near side is visible', () => {
  assert.equal(occludedBySphere(eye, 0, 0, 7, R), false);
});

test('point directly behind the sphere is occluded', () => {
  assert.equal(occludedBySphere(eye, 0, 0, -7, R), true);
});

test('far-side point off the limb is visible', () => {
  assert.equal(occludedBySphere(eye, 10, 0, -7, R), false);
});

test('near-side surface point is visible with a slightly shrunk radius', () => {
  assert.equal(occludedBySphere(eye, 0, 0, R, R * 0.999), false);
});

test('far-side surface point is occluded', () => {
  assert.equal(occludedBySphere(eye, 0, 0, -R, R * 0.999), true);
});
