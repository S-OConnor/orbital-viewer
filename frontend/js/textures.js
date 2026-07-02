// textures.js — async loader for the two vendored NASA Earth basemap images
// (frontend/assets/earth_day.jpg, frontend/assets/earth_night.jpg), uploaded
// as mipmapped WebGL 1 textures.
//
// The source JPEGs are non-power-of-two (5400x2700 and 3600x1800), and
// WebGL 1 requires power-of-two (POT) dimensions to generate mipmaps and use
// REPEAT wrapping. potSizeFor() below picks a POT target size; the decoded
// image is drawn (and thereby resized) into an offscreen 2D canvas at that
// size, and the canvas -- not the original <img> -- is the texImage2D
// source.
//
// Loading is asynchronous and best-effort: createEarthTextures() returns a
// handle synchronously with day/night both null, and fills them in as each
// image finishes loading (or leaves the field null forever on failure). The
// renderer is expected to poll handle.version once per frame and fall back
// to procedural shading until the relevant field is non-null. No failure
// here -- a missing file, a decode error, a WebGL error while uploading --
// is ever allowed to throw out of this module; the page must keep running
// with procedural shading.
//
// IMPORT-SAFE: no DOM / Image / document access at import time, including
// import.meta.url (only read lazily inside createEarthTextures()); nothing
// runs until createEarthTextures() is called.

const ASSET_FILES = { day: 'earth_day.jpg', night: 'earth_night.jpg' };

// Anisotropic filtering is capped here rather than left at the hardware max:
// Earth basemap texels are viewed at extreme grazing angles near the globe
// limb (silhouette edge), where plain trilinear mipmapping blurs badly, and
// 4x is enough to fix that without the cost of a higher cap.
const MAX_ANISOTROPY = 4;

/** Largest power of two <= n (n first floored and clamped to >= 1). */
function largestPotLE(n) {
  const floor = Math.floor(n);
  let p = 1;
  while (p * 2 <= floor) p *= 2;
  return p;
}

/** Floor x to an integer >= 1, or `fallback` if that isn't possible. */
function positiveInt(x, fallback) {
  const n = Math.floor(x);
  return Number.isFinite(n) && n >= 1 ? n : fallback;
}

/**
 * Largest power-of-two dimensions <= maxSize that best fit a w x h source.
 *
 * Rule:
 *  - Each dimension is independently capped to maxSize and floored down to
 *    the largest power of two <= that cap.
 *  - EXCEPT: when the source is (exactly, within floating-point tolerance)
 *    2:1 landscape -- the standard equirectangular aspect ratio of both
 *    vendored Earth images -- height is instead derived as width/2 (always
 *    an exact power of two, since width is). This guarantees the output
 *    stays exactly 2:1 for an equirect source even when maxSize is small
 *    enough to clamp both dimensions to the same cap: independent flooring
 *    alone can't guarantee that, e.g. w=5400,h=2700,maxSize=1024 would
 *    independently floor to 1024x1024 (aspect destroyed) without this rule,
 *    vs. the correct 1024x512.
 *  - width/height are never less than 1; degenerate inputs (<=0, non-finite,
 *    h=0) fall back to the independent-flooring branch and never divide by
 *    zero or return non-finite results.
 *
 * @param {number} w source width in pixels
 * @param {number} h source height in pixels
 * @param {number} maxSize largest allowed output dimension (the caller is
 *   responsible for folding in gl.MAX_TEXTURE_SIZE before calling this)
 * @returns {{width: number, height: number}}
 */
export function potSizeFor(w, h, maxSize) {
  const cap = positiveInt(maxSize, 1);
  const cw = Math.min(positiveInt(w, 1), cap);
  const potW = largestPotLE(cw);

  const EQUIRECT_TOL = 1e-6;
  if (Math.abs(w / h - 2) < EQUIRECT_TOL) {
    return { width: potW, height: Math.max(1, potW / 2) };
  }

  const ch = Math.min(positiveInt(h, 1), cap);
  return { width: potW, height: largestPotLE(ch) };
}

/**
 * Enable anisotropic filtering on the currently-bound TEXTURE_2D if the
 * extension is available (vendor-prefixed forms included for older
 * browsers), capped at MAX_ANISOTROPY. A no-op when unsupported.
 */
function applyAnisotropy(gl) {
  const ext =
    gl.getExtension('EXT_texture_filter_anisotropic') ||
    gl.getExtension('MOZ_EXT_texture_filter_anisotropic') ||
    gl.getExtension('WEBKIT_EXT_texture_filter_anisotropic');
  if (!ext) return;
  const supported = gl.getParameter(ext.MAX_TEXTURE_MAX_ANISOTROPY_EXT);
  gl.texParameterf(gl.TEXTURE_2D, ext.TEXTURE_MAX_ANISOTROPY_EXT, Math.min(MAX_ANISOTROPY, supported));
}

/**
 * Resize `img` onto a POT canvas and upload it as a mipmapped, wrapped
 * WebGL 1 texture. Throws on GL/canvas failure -- the caller (loadOne) is
 * responsible for catching that and warning, so a single bad image can
 * never take the app down.
 */
function uploadTexture(gl, img, canvasFactory, maxSize) {
  const srcW = img.naturalWidth || img.width;
  const srcH = img.naturalHeight || img.height;
  const { width, height } = potSizeFor(srcW, srcH, maxSize);

  const canvas = canvasFactory();
  canvas.width = width;
  canvas.height = height;
  const ctx = canvas.getContext('2d');
  // We deliberately never touch UNPACK_FLIP_Y_WEBGL (it stays at its default
  // false): the canvas 2D coordinate system already has y=0 at the top, so
  // drawing the source image at (0,0) here plus a non-flipped unpack keeps
  // v=0 at the image's top row (north pole), which is what the renderer's
  // UV math assumes.
  ctx.drawImage(img, 0, 0, width, height);

  const tex = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, tex);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGB, gl.RGB, gl.UNSIGNED_BYTE, canvas);

  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
  // S wraps (REPEAT): the renderer samples u across [0.5, 1.5] to straddle
  // the antimeridian seam, which only tiles correctly with REPEAT.
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
  // T never wraps past the poles; CLAMP_TO_EDGE avoids a wraparound seam
  // artifact there instead.
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

  applyAnisotropy(gl);

  gl.generateMipmap(gl.TEXTURE_2D); // safe: source (canvas) is POT
  gl.bindTexture(gl.TEXTURE_2D, null); // restore: leave no texture bound

  return tex;
}

/**
 * Start loading + uploading one of the two basemaps into handle[key].
 * Fire-and-forget: never throws, regardless of how img/canvas/gl fail.
 */
function loadOne(gl, handle, key, basePath, imageFactory, canvasFactory, maxSize) {
  const filename = ASSET_FILES[key];
  let img;
  try {
    img = imageFactory();
  } catch (err) {
    console.warn(`textures: failed to load ${filename}: ${err.message}`);
    return;
  }

  img.onload = () => {
    try {
      handle[key] = uploadTexture(gl, img, canvasFactory, maxSize);
      handle.version += 1;
    } catch (err) {
      console.warn(`textures: failed to load ${filename}: ${err.message}`);
    }
  };
  img.onerror = () => {
    console.warn(`textures: failed to load ${filename}`);
  };

  try {
    img.src = `${basePath}${filename}`;
  } catch (err) {
    console.warn(`textures: failed to load ${filename}: ${err.message}`);
  }
}

/**
 * Kick off loading + GPU upload of the day/night Earth basemaps and return a
 * handle that fills in as each finishes. Never throws: a failed fetch, a
 * decode error, or a WebGL error while uploading leaves the corresponding
 * field null (with exactly one console.warn naming the file) and does not
 * affect the other texture.
 *
 * @param {WebGLRenderingContext} gl
 * @param {object} [opts]
 * @param {string} [opts.basePath] directory (with trailing slash) the two
 *   JPEGs are fetched from. Defaults to frontend/assets/, resolved relative
 *   to this module (not the current page) so it works unchanged from both
 *   index.html and dev/smoke.html.
 * @param {() => HTMLImageElement} [opts.imageFactory] injectable for tests;
 *   defaults to `() => new Image()`.
 * @param {() => HTMLCanvasElement} [opts.canvasFactory] injectable for
 *   tests; defaults to `() => document.createElement('canvas')`.
 * @param {number} [opts.maxSize] upper bound on the POT texture size passed
 *   to potSizeFor(); defaults to 4096 and is further clamped by
 *   gl.getParameter(gl.MAX_TEXTURE_SIZE).
 * @returns {{day: WebGLTexture|null, night: WebGLTexture|null, version: number}}
 */
export function createEarthTextures(gl, opts = {}) {
  const basePath = opts.basePath ?? new URL('../assets/', import.meta.url).href;
  const imageFactory = opts.imageFactory ?? (() => new Image());
  const canvasFactory = opts.canvasFactory ?? (() => document.createElement('canvas'));

  let glMax = 4096;
  try {
    glMax = gl.getParameter(gl.MAX_TEXTURE_SIZE);
  } catch (err) {
    // Defensive only: a real WebGL context never throws for getParameter.
    // Fall back to the module default so a hostile/broken gl stub still
    // can't prevent textures.js from returning a usable handle.
  }
  const maxSize = Math.min(opts.maxSize ?? 4096, positiveInt(glMax, 4096));

  const handle = { day: null, night: null, version: 0 };

  for (const key of Object.keys(ASSET_FILES)) {
    loadOne(gl, handle, key, basePath, imageFactory, canvasFactory, maxSize);
  }

  return handle;
}
