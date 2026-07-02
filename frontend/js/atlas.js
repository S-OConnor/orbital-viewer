// atlas.js — procedural sprite-atlas painter: first-party vector art for all
// object-category icons plus a full-color primary-satellite marker, no image
// assets and no third-party code.
//
// The atlas is a 4x4 grid of square cells (ATLAS_GRID x ATLAS_GRID), CELL px
// on a side at 1x scale; paintAtlas() takes any cellPx so a caller can
// rasterize at device-pixel-ratio resolution for crisp edges. Row 0 is the
// TOP row of the image (canvas convention), consistent with iconUV()'s v
// axis (v=0 = image top).
//
// Category icons (debris, star, comet, satellite, groundHot) are painted
// pure white — rgba(255,255,255,*), only alpha ever varies — so the
// renderer can recolor them per object by multiplying the vertex color in
// the shader (this is also how groundHot gets its per-object heat color).
// satMarker is the one full-color cell: detailed satellite artwork that
// replaces the plain yellow-blob primary-satellite marker; it is drawn with
// its own fixed palette and is never tinted by the renderer.
//
// IMPORT-SAFE: this module touches no DOM/canvas/WebGL at import time.
// paintAtlas() only ever calls methods on the ctx passed in by its caller;
// nothing runs until that happens.

export const ATLAS_GRID = 4;   // atlas is a 4x4 grid of square cells
export const CELL = 64;        // base cell size in px (callers scale by DPR)

// Cell indices, row-major, row 0 at the TOP of the image.
export const ICONS = {
  debris: 0, star: 1, comet: 2, satellite: 3, groundHot: 4, satMarker: 5,
};

// Fractional padding kept empty on each side of a cell before an icon's
// silhouette begins (icons are centred, ~10-15% padding per spec).
const PAD = 0.13;

// White used throughout the category icons. Only rgba(255,255,255,*) ever
// appears as a fillStyle/strokeStyle in the icons below (index 0-4) — the
// renderer depends on this to tint them per object.
const WHITE = 'rgba(255,255,255,1)';

/**
 * debris (index 0): an irregular angular shard/fragment silhouette — a
 * deliberately asymmetric polygon (not a regular star/gem) so it reads as
 * "broken debris" rather than a decorative shape.
 */
function drawDebris(ctx, cellPx) {
  const c = cellPx / 2;
  const r = cellPx * (0.5 - PAD);
  // [angleDeg, radiusFraction] pairs walked in order to form one jagged,
  // non-convex outline.
  const pts = [
    [-60, 1.00], [-10, 0.55], [35, 0.95], [80, 0.40],
    [140, 0.85], [190, 0.35], [230, 0.70], [280, 0.30],
  ];
  ctx.fillStyle = WHITE;
  ctx.beginPath();
  pts.forEach(([deg, frac], i) => {
    const a = (deg * Math.PI) / 180;
    const x = c + Math.cos(a) * r * frac;
    const y = c + Math.sin(a) * r * frac;
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });
  ctx.closePath();
  ctx.fill();
}

/**
 * star (index 1): a 4-point sparkle / diffraction-spike star — alternating
 * long (N/E/S/W) and short (diagonal) spikes around a small bright core.
 */
function drawStar(ctx, cellPx) {
  const c = cellPx / 2;
  const outerLong = cellPx * (0.5 - PAD);
  const outerShort = outerLong * 0.32;
  const inner = outerLong * 0.14;

  ctx.fillStyle = WHITE;
  ctx.beginPath();
  for (let i = 0; i < 8; i++) {
    const angle = (Math.PI / 4) * i - Math.PI / 2; // spike 0 points up
    const outer = i % 2 === 0 ? outerLong : outerShort;
    const x1 = c + Math.cos(angle) * outer;
    const y1 = c + Math.sin(angle) * outer;
    if (i === 0) ctx.moveTo(x1, y1);
    else ctx.lineTo(x1, y1);
    // Concave point between this spike and the next, pulled in close to
    // the core so spikes read as thin and pointed rather than a blob.
    const midAngle = angle + Math.PI / 8;
    ctx.lineTo(c + Math.cos(midAngle) * inner, c + Math.sin(midAngle) * inner);
  }
  ctx.closePath();
  ctx.fill();
}

/**
 * comet (index 2): a bright nucleus with a swept tail. The tail is built
 * from a few overlapping triangular strips of decreasing alpha (rather than
 * a canvas gradient) so it stays strictly "white, alpha varies" — a
 * gradient object cannot be recolored by the renderer's flat vertex tint.
 */
function drawComet(ctx, cellPx) {
  const nucleusR = cellPx * 0.11;
  const headX = cellPx * (1 - PAD - 0.08);
  const headY = cellPx * PAD + nucleusR;
  const tailTipX = cellPx * PAD;
  const tailTipY = cellPx * (1 - PAD);
  const halfWidth = cellPx * 0.16;

  const dx = tailTipX - headX;
  const dy = tailTipY - headY;
  const len = Math.hypot(dx, dy) || 1;
  const px = -dy / len; // unit perpendicular to the head->tail axis
  const py = dx / len;

  // Layers ordered far-from-head (wide, faint) to near-head (narrow, more
  // opaque) so overlapping fills approximate a soft taper.
  const layers = [
    { t: 1.0, alpha: 0.18 },
    { t: 0.66, alpha: 0.35 },
    { t: 0.33, alpha: 0.6 },
  ];
  for (const { t, alpha } of layers) {
    const midX = headX + dx * t;
    const midY = headY + dy * t;
    const w = halfWidth * t;
    ctx.fillStyle = `rgba(255,255,255,${alpha})`;
    ctx.beginPath();
    ctx.moveTo(headX, headY);
    ctx.lineTo(midX + px * w, midY + py * w);
    ctx.lineTo(midX - px * w, midY - py * w);
    ctx.closePath();
    ctx.fill();
  }

  ctx.fillStyle = WHITE;
  ctx.beginPath();
  ctx.arc(headX, headY, nucleusR, 0, Math.PI * 2);
  ctx.fill();
}

/**
 * satellite (index 3): a small glyph — rectangular bus with two solar-panel
 * wings — legible as a plain silhouette at 12-16 CSS px.
 */
function drawSatelliteGlyph(ctx, cellPx) {
  const c = cellPx / 2;
  const busW = cellPx * 0.22;
  const busH = cellPx * 0.30;
  const wingW = cellPx * 0.26;
  const wingH = cellPx * 0.16;
  const wingGap = cellPx * 0.03;

  ctx.fillStyle = WHITE;

  ctx.beginPath();
  ctx.rect(c - busW / 2, c - busH / 2, busW, busH);
  ctx.fill();

  ctx.beginPath();
  ctx.rect(c - busW / 2 - wingGap - wingW, c - wingH / 2, wingW, wingH);
  ctx.fill();

  ctx.beginPath();
  ctx.rect(c + busW / 2 + wingGap, c - wingH / 2, wingW, wingH);
  ctx.fill();

  // Thin struts joining the wings to the bus, needed for readability once
  // the wing/bus gap is only a few px at 12-16 CSS px render size.
  ctx.strokeStyle = WHITE;
  ctx.lineWidth = Math.max(1, cellPx * 0.02);
  ctx.beginPath();
  ctx.moveTo(c - busW / 2 - wingGap, c);
  ctx.lineTo(c - busW / 2, c);
  ctx.moveTo(c + busW / 2, c);
  ctx.lineTo(c + busW / 2 + wingGap, c);
  ctx.stroke();
}

/**
 * groundHot (index 4): a heat-plume/flame glyph — teardrop silhouette from
 * two mirrored bezier curves, with a lower-alpha inner core so the shape
 * still reads as two-tone once the renderer tints it with a per-object
 * temperature color (see renderer.js groundHotColor()).
 */
function drawGroundHot(ctx, cellPx) {
  const c = cellPx / 2;
  const top = cellPx * PAD;
  const bottom = cellPx * (1 - PAD);
  const w = cellPx * (0.5 - PAD) * 0.9;

  ctx.fillStyle = WHITE;
  ctx.beginPath();
  ctx.moveTo(c, top);
  ctx.bezierCurveTo(
    c + w, top + (bottom - top) * 0.35,
    c + w * 0.55, bottom * 0.7 + top * 0.3,
    c, bottom,
  );
  ctx.bezierCurveTo(
    c - w * 0.55, bottom * 0.7 + top * 0.3,
    c - w, top + (bottom - top) * 0.35,
    c, top,
  );
  ctx.closePath();
  ctx.fill();

  const innerTop = top + (bottom - top) * 0.30;
  const innerW = w * 0.5;
  ctx.fillStyle = 'rgba(255,255,255,0.55)';
  ctx.beginPath();
  ctx.moveTo(c, innerTop);
  ctx.bezierCurveTo(
    c + innerW, innerTop + (bottom - innerTop) * 0.4,
    c + innerW * 0.5, bottom * 0.85,
    c, bottom * 0.96,
  );
  ctx.bezierCurveTo(
    c - innerW * 0.5, bottom * 0.85,
    c - innerW, innerTop + (bottom - innerTop) * 0.4,
    c, innerTop,
  );
  ctx.closePath();
  ctx.fill();
}

/**
 * satMarker (index 5): full-color detailed satellite artwork for the
 * primary-satellite marker (replaces the plain yellow blob) — grey/white
 * bus, dark-blue solar-panel wings with a visible cell grid, a small
 * gold/foil accent, and a subtle white outline so it reads against both the
 * black of space and the bright limb of the Earth. Legible at 28-32 CSS px.
 */
function drawSatMarker(ctx, cellPx) {
  const c = cellPx / 2;
  const bodyW = cellPx * 0.20;
  const bodyH = cellPx * 0.40;
  const wingW = cellPx * 0.30;
  const wingH = cellPx * 0.20;
  const wingGap = cellPx * 0.03;
  const outlineW = Math.max(1, cellPx * 0.025);
  const outlineColor = 'rgba(255,255,255,0.85)';

  const wings = [
    { x: c - bodyW / 2 - wingGap - wingW, y: c - wingH / 2 },
    { x: c + bodyW / 2 + wingGap, y: c - wingH / 2 },
  ];
  for (const wing of wings) {
    ctx.fillStyle = '#13294b'; // dark-blue panel substrate
    ctx.strokeStyle = outlineColor;
    ctx.lineWidth = outlineW;
    ctx.beginPath();
    ctx.rect(wing.x, wing.y, wingW, wingH);
    ctx.fill();
    ctx.stroke();

    // Cell-grid lines so the panel reads as individual solar cells rather
    // than a flat rectangle at 28-32 CSS px.
    ctx.strokeStyle = 'rgba(180,200,230,0.9)';
    ctx.lineWidth = Math.max(0.5, cellPx * 0.01);
    const cols = 4;
    const rows = 2;
    ctx.beginPath();
    for (let i = 1; i < cols; i++) {
      const gx = wing.x + (wingW * i) / cols;
      ctx.moveTo(gx, wing.y);
      ctx.lineTo(gx, wing.y + wingH);
    }
    for (let j = 1; j < rows; j++) {
      const gy = wing.y + (wingH * j) / rows;
      ctx.moveTo(wing.x, gy);
      ctx.lineTo(wing.x + wingW, gy);
    }
    ctx.stroke();
  }

  // Bus (body): light grey with a subtle white outline.
  ctx.fillStyle = '#c9ced6';
  ctx.strokeStyle = outlineColor;
  ctx.lineWidth = outlineW;
  ctx.beginPath();
  ctx.rect(c - bodyW / 2, c - bodyH / 2, bodyW, bodyH);
  ctx.fill();
  ctx.stroke();

  // Gold/foil thermal-blanket accent on the sun-facing end of the bus.
  ctx.fillStyle = '#d4af37';
  ctx.beginPath();
  ctx.rect(c - bodyW / 2, c - bodyH / 2, bodyW, bodyH * 0.22);
  ctx.fill();
  ctx.stroke();

  // Struts joining wings to the bus.
  ctx.strokeStyle = outlineColor;
  ctx.lineWidth = outlineW;
  ctx.beginPath();
  ctx.moveTo(c - bodyW / 2 - wingGap, c);
  ctx.lineTo(c - bodyW / 2, c);
  ctx.moveTo(c + bodyW / 2, c);
  ctx.lineTo(c + bodyW / 2 + wingGap, c);
  ctx.stroke();
}

// Cell index -> drawing helper, for the 6 defined icons. Cells 6-15 are left
// out of this map and therefore stay untouched/transparent by paintAtlas().
const PAINTERS = {
  [ICONS.debris]: drawDebris,
  [ICONS.star]: drawStar,
  [ICONS.comet]: drawComet,
  [ICONS.satellite]: drawSatelliteGlyph,
  [ICONS.groundHot]: drawGroundHot,
  [ICONS.satMarker]: drawSatMarker,
};

/**
 * Paint every defined icon into ctx, a CanvasRenderingContext2D-compatible
 * object for a square canvas of size (ATLAS_GRID * cellPx). Clears/fills
 * nothing outside icon shapes (transparent background); cells with no
 * entry in PAINTERS (6-15) are never touched. Deterministic: identical
 * cellPx produces an identical sequence of ctx calls every time.
 *
 * Note: Object.entries() on PAINTERS always yields its integer-like keys in
 * ascending numeric order (per the ECMAScript property-order rules), so
 * icons are painted in index order 0..5 regardless of PAINTERS' literal
 * layout above.
 *
 * @param {CanvasRenderingContext2D} ctx target context (or compatible stub)
 * @param {number} cellPx cell size in px (callers scale by DPR)
 */
export function paintAtlas(ctx, cellPx) {
  for (const [indexStr, paint] of Object.entries(PAINTERS)) {
    const index = Number(indexStr);
    const col = index % ATLAS_GRID;
    const row = Math.floor(index / ATLAS_GRID);
    ctx.save();
    ctx.translate(col * cellPx, row * cellPx);
    paint(ctx, cellPx);
    ctx.restore();
  }
}

/**
 * Pure. UV rect for a cell index in IMAGE space (v=0 = image top), inset by
 * ~4% of the cell on each side to prevent texture bleed/bilinear smear
 * between neighboring atlas cells.
 *
 * @param {number} index cell index, must be an integer in
 *   [0, ATLAS_GRID*ATLAS_GRID - 1]
 * @returns {{u0: number, v0: number, u1: number, v1: number}}
 * @throws {RangeError} on a non-integer or out-of-range index — chosen over
 *   silently clamping so a caller bug never silently renders the wrong
 *   icon.
 */
export function iconUV(index) {
  const maxIndex = ATLAS_GRID * ATLAS_GRID - 1;
  if (!Number.isInteger(index) || index < 0 || index > maxIndex) {
    throw new RangeError(`iconUV: index must be an integer in [0, ${maxIndex}], got ${index}`);
  }
  const cellFrac = 1 / ATLAS_GRID;
  const inset = cellFrac * 0.04;
  const col = index % ATLAS_GRID;
  const row = Math.floor(index / ATLAS_GRID);
  return {
    u0: col * cellFrac + inset,
    v0: row * cellFrac + inset,
    u1: (col + 1) * cellFrac - inset,
    v1: (row + 1) * cellFrac - inset,
  };
}
