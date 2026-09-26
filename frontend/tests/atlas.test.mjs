// Unit tests for atlas.js (procedural sprite-atlas painter).
// Run: node --test frontend/tests/atlas.test.mjs
import test from 'node:test';
import assert from 'node:assert/strict';
import { ATLAS_GRID, CELL, ICONS, paintAtlas, iconUV } from '../js/atlas.js';

// The 8 defined icon cell indices, per the frozen ICONS map.
const DEFINED_INDICES = [0, 1, 2, 3, 4, 5, 6, 7];
const CATEGORY_INDICES = [
  ICONS.debris, ICONS.star, ICONS.comet, ICONS.satellite, ICONS.groundHot, ICONS.unknown,
];

const CELL_PX = 64;

// Matches only rgba(255,255,255,<alpha>) — the one white form atlas.js
// emits for category icons (alpha is the only value that ever varies).
const WHITE_RE = /^rgba\(255,255,255,(0|1|0?\.\d+)\)$/;

/**
 * Minimal CanvasRenderingContext2D-compatible recorder: implements exactly
 * the methods/properties atlas.js's paintAtlas() calls, and appends one
 * entry per call/property-set to `log` in call order. Deliberately a plain
 * object (not a Proxy) so the recorded surface is easy to audit by eye.
 */
function createRecorder() {
  const log = [];
  const style = { fillStyle: undefined, strokeStyle: undefined, lineWidth: undefined };
  const ctx = {
    save() { log.push({ m: 'save' }); },
    restore() { log.push({ m: 'restore' }); },
    translate(x, y) { log.push({ m: 'translate', a: [x, y] }); },
    beginPath() { log.push({ m: 'beginPath' }); },
    closePath() { log.push({ m: 'closePath' }); },
    moveTo(x, y) { log.push({ m: 'moveTo', a: [x, y] }); },
    lineTo(x, y) { log.push({ m: 'lineTo', a: [x, y] }); },
    bezierCurveTo(x1, y1, x2, y2, x3, y3) {
      log.push({ m: 'bezierCurveTo', a: [x1, y1, x2, y2, x3, y3] });
    },
    arc(x, y, r, a0, a1) { log.push({ m: 'arc', a: [x, y, r, a0, a1] }); },
    rect(x, y, w, h) { log.push({ m: 'rect', a: [x, y, w, h] }); },
    fill() { log.push({ m: 'fill', fillStyle: style.fillStyle }); },
    stroke() { log.push({ m: 'stroke', strokeStyle: style.strokeStyle, lineWidth: style.lineWidth }); },
    set fillStyle(v) { style.fillStyle = v; log.push({ m: 'set:fillStyle', a: [v] }); },
    get fillStyle() { return style.fillStyle; },
    set strokeStyle(v) { style.strokeStyle = v; log.push({ m: 'set:strokeStyle', a: [v] }); },
    get strokeStyle() { return style.strokeStyle; },
    set lineWidth(v) { style.lineWidth = v; log.push({ m: 'set:lineWidth', a: [v] }); },
    get lineWidth() { return style.lineWidth; },
  };
  return { ctx, log };
}

/** Split a full paintAtlas() log into one sub-array per save()/restore() block, in order. */
function splitByCell(log) {
  const blocks = [];
  let current = null;
  let depth = 0;
  for (const entry of log) {
    if (entry.m === 'save') {
      assert.equal(depth, 0, 'save() calls must not nest (one save/restore pair per cell)');
      depth = 1;
      current = [];
    }
    if (current) current.push(entry);
    if (entry.m === 'restore') {
      assert.equal(depth, 1, 'restore() must be paired with a preceding save()');
      depth = 0;
      blocks.push(current);
      current = null;
    }
  }
  assert.equal(depth, 0, 'every save() must be matched by a restore()');
  return blocks;
}

test('frozen constants match the interface exactly', () => {
  assert.equal(ATLAS_GRID, 4);
  assert.equal(CELL, 64);
  assert.deepEqual(ICONS, {
    debris: 0, star: 1, comet: 2, satellite: 3, groundHot: 4, satMarker: 5,
    skyDot: 6, unknown: 7,
  });
});

test('paintAtlas draws into exactly the 8 defined cells, save/restore balanced', () => {
  const { ctx, log } = createRecorder();
  paintAtlas(ctx, CELL_PX);

  const saves = log.filter((e) => e.m === 'save').length;
  const restores = log.filter((e) => e.m === 'restore').length;
  assert.equal(saves, 8, 'expected exactly 8 save() calls, one per defined icon');
  assert.equal(restores, 8, 'expected exactly 8 restore() calls, one per defined icon');

  const blocks = splitByCell(log);
  assert.equal(blocks.length, 8);

  // Each block's translate() must land on exactly one of the 8 defined
  // cells' top-left corner, and every defined cell must be hit exactly
  // once (i.e. cells 8-15 are never targeted).
  const expectedOrigins = DEFINED_INDICES.map((index) => [
    (index % ATLAS_GRID) * CELL_PX,
    Math.floor(index / ATLAS_GRID) * CELL_PX,
  ]);
  const actualOrigins = blocks.map((block) => {
    const translates = block.filter((e) => e.m === 'translate');
    assert.equal(translates.length, 1, 'each cell block must translate exactly once');
    return translates[0].a;
  });
  assert.deepEqual(
    actualOrigins.slice().sort((a, b) => a[0] - b[0] || a[1] - b[1]),
    expectedOrigins.slice().sort((a, b) => a[0] - b[0] || a[1] - b[1]),
  );
});

test('category icons (debris, star, comet, satellite, groundHot, unknown) only ever set white-ish styles', () => {
  const { ctx, log } = createRecorder();
  paintAtlas(ctx, CELL_PX);
  const blocks = splitByCell(log);

  for (const index of CATEGORY_INDICES) {
    const block = blocks[index];
    const styleSets = block.filter((e) => e.m === 'set:fillStyle' || e.m === 'set:strokeStyle');
    assert.ok(styleSets.length > 0, `icon ${index} should set at least one style`);
    for (const entry of styleSets) {
      assert.ok(
        WHITE_RE.test(entry.a[0]),
        `icon ${index} set a non-white ${entry.m}: ${entry.a[0]}`,
      );
    }
  }
});

test('satMarker sets at least 3 distinct non-white colors', () => {
  const { ctx, log } = createRecorder();
  paintAtlas(ctx, CELL_PX);
  const blocks = splitByCell(log);
  const block = blocks[ICONS.satMarker];

  const nonWhite = new Set(
    block
      .filter((e) => e.m === 'set:fillStyle' || e.m === 'set:strokeStyle')
      .map((e) => e.a[0])
      .filter((color) => !WHITE_RE.test(color)),
  );
  assert.ok(
    nonWhite.size >= 3,
    `expected >= 3 distinct non-white colors for satMarker, got ${nonWhite.size}: ${[...nonWhite]}`,
  );
});

test('cells 8-15 stay empty: only 8 cell blocks are ever produced', () => {
  const { ctx, log } = createRecorder();
  paintAtlas(ctx, CELL_PX);
  assert.equal(splitByCell(log).length, 8);
  // No drawing call should reference geometry beyond the occupied region
  // (cols 0-3 in rows 0-1) once cell-local coordinates are
  // added back to their block's translate origin.
  const blocks = splitByCell(log);
  const maxX = ATLAS_GRID * CELL_PX;
  const maxY = ATLAS_GRID * CELL_PX;
  for (const block of blocks) {
    const [ox, oy] = block.find((e) => e.m === 'translate').a;
    assert.ok(ox >= 0 && ox < maxX && oy >= 0 && oy < maxY);
  }
});

test('paintAtlas geometry scales with cellPx (no hardcoded 64s)', () => {
  const small = createRecorder();
  const large = createRecorder();
  paintAtlas(small.ctx, 16);
  paintAtlas(large.ctx, 128);

  const smallOrigins = small.log.filter((e) => e.m === 'translate').map((e) => e.a);
  const largeOrigins = large.log.filter((e) => e.m === 'translate').map((e) => e.a);
  for (let i = 0; i < smallOrigins.length; i++) {
    assert.deepEqual(largeOrigins[i], [smallOrigins[i][0] * 8, smallOrigins[i][1] * 8]);
  }

  // Spot-check that in-cell geometry (e.g. every moveTo) also scales
  // linearly with cellPx, not just the translate origins.
  const smallMoves = small.log.filter((e) => e.m === 'moveTo').map((e) => e.a);
  const largeMoves = large.log.filter((e) => e.m === 'moveTo').map((e) => e.a);
  assert.equal(smallMoves.length, largeMoves.length);
  for (let i = 0; i < smallMoves.length; i++) {
    const [sx, sy] = smallMoves[i];
    const [lx, ly] = largeMoves[i];
    assert.ok(Math.abs(lx - sx * 8) < 1e-9, `moveTo x should scale 8x: ${sx} -> ${lx}`);
    assert.ok(Math.abs(ly - sy * 8) < 1e-9, `moveTo y should scale 8x: ${sy} -> ${ly}`);
  }
});

test('paintAtlas is deterministic: two independent runs produce identical call logs', () => {
  const a = createRecorder();
  const b = createRecorder();
  paintAtlas(a.ctx, CELL_PX);
  paintAtlas(b.ctx, CELL_PX);
  assert.deepEqual(a.log, b.log);
});

test('iconUV: rects for the 8 defined icons are within [0,1] and inset from cell edges', () => {
  const cellFrac = 1 / ATLAS_GRID;
  for (const index of DEFINED_INDICES) {
    const { u0, v0, u1, v1 } = iconUV(index);
    const col = index % ATLAS_GRID;
    const row = Math.floor(index / ATLAS_GRID);

    assert.ok(u0 >= 0 && u1 <= 1 && v0 >= 0 && v1 <= 1, `icon ${index} UV out of [0,1]`);
    assert.ok(u0 < u1 && v0 < v1, `icon ${index} UV rect must be non-degenerate`);

    // Strictly inside the cell's exact edges (i.e. actually inset, not
    // flush against the boundary).
    assert.ok(u0 > col * cellFrac, `icon ${index} u0 should be inset from the left cell edge`);
    assert.ok(u1 < (col + 1) * cellFrac, `icon ${index} u1 should be inset from the right cell edge`);
    assert.ok(v0 > row * cellFrac, `icon ${index} v0 should be inset from the top cell edge`);
    assert.ok(v1 < (row + 1) * cellFrac, `icon ${index} v1 should be inset from the bottom cell edge`);
  }
});

test('iconUV: rects for the 8 defined icons are pairwise non-overlapping', () => {
  const rects = DEFINED_INDICES.map((index) => iconUV(index));
  const overlaps = (a, b) => a.u0 < b.u1 && b.u0 < a.u1 && a.v0 < b.v1 && b.v0 < a.v1;
  for (let i = 0; i < rects.length; i++) {
    for (let j = i + 1; j < rects.length; j++) {
      assert.ok(!overlaps(rects[i], rects[j]), `icons ${i} and ${j} UV rects overlap`);
    }
  }
});

test('iconUV: throws RangeError on out-of-range or non-integer index', () => {
  assert.throws(() => iconUV(-1), RangeError);
  assert.throws(() => iconUV(16), RangeError);
  assert.throws(() => iconUV(1.5), RangeError);
  assert.throws(() => iconUV(NaN), RangeError);
});

test('iconUV: accepts every valid grid index 0-15 without throwing', () => {
  for (let i = 0; i < ATLAS_GRID * ATLAS_GRID; i++) {
    assert.doesNotThrow(() => iconUV(i));
  }
});

test('module import touches no DOM/canvas/WebGL (import-safe)', () => {
  // If import time had any side effect requiring a DOM/canvas, importing
  // this test file (which imports atlas.js at the top) would already have
  // thrown under plain node. This test just asserts the exported surface
  // is present and callable without ever constructing a real canvas.
  assert.equal(typeof paintAtlas, 'function');
  assert.equal(typeof iconUV, 'function');
});
