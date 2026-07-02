import test from 'node:test';
import assert from 'node:assert/strict';
import { potSizeFor, createEarthTextures } from '../js/textures.js';

// ---------------------------------------------------------------------------
// Fakes
// ---------------------------------------------------------------------------

// Real WebGL 1 enum values (not just placeholders) so this stub behaves like
// a real gl if ever swapped in, and so texParameteri call assertions below
// can compare against gl.<CONST> the same way production code does.
const GL_ENUM = {
  TEXTURE_2D: 0x0de1,
  RGB: 0x1907,
  UNSIGNED_BYTE: 0x1401,
  LINEAR_MIPMAP_LINEAR: 0x2703,
  LINEAR: 0x2601,
  TEXTURE_MIN_FILTER: 0x2801,
  TEXTURE_MAG_FILTER: 0x2800,
  TEXTURE_WRAP_S: 0x2802,
  TEXTURE_WRAP_T: 0x2803,
  REPEAT: 0x2901,
  CLAMP_TO_EDGE: 0x812f,
  MAX_TEXTURE_SIZE: 0x0d33,
};

/**
 * Stub WebGL 1 context. Records createTexture/bindTexture/texParameteri
 * (and texParameterf)/texImage2D/generateMipmap calls for assertions.
 * getParameter(MAX_TEXTURE_SIZE) -> maxTextureSize (default 4096).
 * getExtension -> extension (default null, i.e. "not supported").
 */
function makeFakeGl({ maxTextureSize = 4096, extension = null, throwOnCreateTexture = false } = {}) {
  const calls = {
    createTexture: 0,
    bindTexture: [],
    texParameteri: [], // [pname, value]
    texImage2D: [], // full arg lists
    generateMipmap: [],
  };
  let nextTexId = 1;

  const gl = {
    ...GL_ENUM,
    calls,
    createTexture() {
      if (throwOnCreateTexture) throw new Error('simulated GL failure');
      calls.createTexture += 1;
      return { __texId: nextTexId++ };
    },
    bindTexture(target, tex) {
      calls.bindTexture.push(tex);
    },
    texParameteri(target, pname, value) {
      calls.texParameteri.push([pname, value]);
    },
    texParameterf(target, pname, value) {
      calls.texParameteri.push([pname, value]);
    },
    texImage2D(...args) {
      calls.texImage2D.push(args);
    },
    generateMipmap(target) {
      calls.generateMipmap.push(target);
    },
    getParameter(pname) {
      if (pname === GL_ENUM.MAX_TEXTURE_SIZE) return maxTextureSize;
      if (extension && pname === extension.MAX_TEXTURE_MAX_ANISOTROPY_EXT) return extension.supported;
      return null;
    },
    getExtension() {
      return extension;
    },
  };
  return gl;
}

/** Fake `() => new Image()` factory. Exposes .instances for the test to drive. */
function makeFakeImageFactory() {
  const instances = [];
  const factory = () => {
    const img = {
      src: '',
      naturalWidth: 0,
      naturalHeight: 0,
      onload: null,
      onerror: null,
    };
    instances.push(img);
    return img;
  };
  factory.instances = instances;
  return factory;
}

/** Fake `() => document.createElement('canvas')` factory. Records drawImage calls. */
function makeFakeCanvasFactory() {
  const draws = [];
  const factory = () => {
    const canvas = {
      width: 0,
      height: 0,
      getContext() {
        return {
          drawImage(...args) {
            draws.push({ canvas, args });
          },
        };
      },
    };
    return canvas;
  };
  factory.draws = draws;
  return factory;
}

function withStubbedWarn(fn) {
  return async () => {
    const calls = [];
    const original = console.warn;
    console.warn = (...args) => calls.push(args.join(' '));
    try {
      await fn(calls);
    } finally {
      console.warn = original;
    }
  };
}

/** Find the fake Image instance whose .src was set for `filename`. */
function findImage(imageFactory, filename) {
  return imageFactory.instances.find((img) => img.src.endsWith(filename));
}

// ---------------------------------------------------------------------------
// potSizeFor
// ---------------------------------------------------------------------------

test('potSizeFor: 5400x2700 (day, 2:1) @ maxSize 4096 -> 4096x2048', () => {
  assert.deepEqual(potSizeFor(5400, 2700, 4096), { width: 4096, height: 2048 });
});

test('potSizeFor: 3600x1800 (night, 2:1) @ maxSize 4096 -> 2048x1024', () => {
  // Documented rule: width floors to the largest POT <= min(w, maxSize)
  // (3600 -> 2048, since 4096 > 3600); because the source is exactly 2:1,
  // height is derived as width/2 rather than floored independently.
  assert.deepEqual(potSizeFor(3600, 1800, 4096), { width: 2048, height: 1024 });
});

test('potSizeFor: small 2:1 source 100x50 @ maxSize 4096 -> 64x32', () => {
  assert.deepEqual(potSizeFor(100, 50, 4096), { width: 64, height: 32 });
});

test('potSizeFor: maxSize clamps both dimensions for a 2:1 source (1024) -> 1024x512', () => {
  // Regression case for the rule documented in textures.js: independently
  // flooring w and h would give 1024x1024 here (aspect destroyed) since
  // both 5400 and 2700 exceed the 1024 cap; deriving height from width/2
  // keeps the exact 2:1 aspect instead.
  assert.deepEqual(potSizeFor(5400, 2700, 1024), { width: 1024, height: 512 });
});

test('potSizeFor: non-2:1 source floors each dimension independently', () => {
  // 800x600 (4:3) @ maxSize 4096: 800 -> 512, 600 -> 512 (independent).
  assert.deepEqual(potSizeFor(800, 600, 4096), { width: 512, height: 512 });
});

test('potSizeFor: exact power-of-two 2:1 source is unchanged (below cap)', () => {
  // 512x256 is already POT and exactly 2:1, so both branches of the rule
  // agree: width floors to 512, and height = width / 2 = 256.
  assert.deepEqual(potSizeFor(512, 256, 4096), { width: 512, height: 256 });
});

test('potSizeFor: degenerate inputs never throw or return non-finite values', () => {
  for (const [w, h, maxSize] of [
    [0, 0, 4096],
    [-5, -5, 4096],
    [5400, 0, 4096],
    [0, 2700, 4096],
    [5400, 2700, 0],
    [5400, 2700, -1],
    [NaN, 2700, 4096],
    [5400, NaN, 4096],
    [5400, 2700, NaN],
  ]) {
    const result = potSizeFor(w, h, maxSize);
    assert.ok(Number.isFinite(result.width) && result.width >= 1, `width for (${w},${h},${maxSize})`);
    assert.ok(Number.isFinite(result.height) && result.height >= 1, `height for (${w},${h},${maxSize})`);
  }
});

// ---------------------------------------------------------------------------
// createEarthTextures
// ---------------------------------------------------------------------------

test('createEarthTextures: initial handle is null/null/version 0', () => {
  const gl = makeFakeGl();
  const imageFactory = makeFakeImageFactory();
  const canvasFactory = makeFakeCanvasFactory();
  const handle = createEarthTextures(gl, { imageFactory, canvasFactory });

  assert.equal(handle.day, null);
  assert.equal(handle.night, null);
  assert.equal(handle.version, 0);
  assert.equal(imageFactory.instances.length, 2);
});

test('createEarthTextures: day onload fills day, bumps version, sets correct GL params', () => {
  const gl = makeFakeGl();
  const imageFactory = makeFakeImageFactory();
  const canvasFactory = makeFakeCanvasFactory();
  const handle = createEarthTextures(gl, { imageFactory, canvasFactory });

  const dayImg = findImage(imageFactory, 'earth_day.jpg');
  dayImg.naturalWidth = 5400;
  dayImg.naturalHeight = 2700;
  dayImg.onload();

  assert.notEqual(handle.day, null);
  assert.equal(handle.night, null);
  assert.equal(handle.version, 1);

  const params = gl.calls.texParameteri;
  assert.ok(params.some(([p, v]) => p === gl.TEXTURE_WRAP_S && v === gl.REPEAT), 'TEXTURE_WRAP_S = REPEAT');
  assert.ok(
    params.some(([p, v]) => p === gl.TEXTURE_WRAP_T && v === gl.CLAMP_TO_EDGE),
    'TEXTURE_WRAP_T = CLAMP_TO_EDGE'
  );
  assert.ok(
    params.some(([p, v]) => p === gl.TEXTURE_MIN_FILTER && v === gl.LINEAR_MIPMAP_LINEAR),
    'TEXTURE_MIN_FILTER = LINEAR_MIPMAP_LINEAR'
  );
  assert.ok(
    params.some(([p, v]) => p === gl.TEXTURE_MAG_FILTER && v === gl.LINEAR),
    'TEXTURE_MAG_FILTER = LINEAR'
  );
  assert.equal(gl.calls.generateMipmap.length, 1);
  assert.equal(gl.calls.createTexture, 1);
  // bindTexture(..., null) restores state at the end of the upload.
  assert.equal(gl.calls.bindTexture[gl.calls.bindTexture.length - 1], null);

  // Canvas was sized to potSizeFor(5400, 2700, 4096) = 4096x2048 and that
  // canvas (not the raw <img>) is the texImage2D source.
  const [, , internalFormat, format, type, source] = gl.calls.texImage2D[0];
  assert.equal(internalFormat, gl.RGB);
  assert.equal(format, gl.RGB);
  assert.equal(type, gl.UNSIGNED_BYTE);
  assert.equal(source.width, 4096);
  assert.equal(source.height, 2048);
});

test(
  'createEarthTextures: night onerror leaves night null, version unchanged, exactly one warn',
  withStubbedWarn(async (calls) => {
    const gl = makeFakeGl();
    const imageFactory = makeFakeImageFactory();
    const canvasFactory = makeFakeCanvasFactory();
    const handle = createEarthTextures(gl, { imageFactory, canvasFactory });

    // Establish a baseline version via a successful day load first, so we can
    // confirm the failed night load truly leaves version untouched.
    const dayImg = findImage(imageFactory, 'earth_day.jpg');
    dayImg.naturalWidth = 5400;
    dayImg.naturalHeight = 2700;
    dayImg.onload();
    assert.equal(handle.version, 1);

    const nightImg = findImage(imageFactory, 'earth_night.jpg');
    nightImg.onerror();

    assert.equal(handle.night, null);
    assert.equal(handle.version, 1);
    assert.equal(calls.length, 1);
    assert.match(calls[0], /earth_night\.jpg/);
  })
);

test(
  'createEarthTextures: thrown GL error during upload leaves field null with one warn',
  withStubbedWarn(async (calls) => {
    const gl = makeFakeGl({ throwOnCreateTexture: true });
    const imageFactory = makeFakeImageFactory();
    const canvasFactory = makeFakeCanvasFactory();
    const handle = createEarthTextures(gl, { imageFactory, canvasFactory });

    const dayImg = findImage(imageFactory, 'earth_day.jpg');
    dayImg.naturalWidth = 5400;
    dayImg.naturalHeight = 2700;
    assert.doesNotThrow(() => dayImg.onload());

    assert.equal(handle.day, null);
    assert.equal(handle.version, 0);
    assert.equal(calls.length, 1);
    assert.match(calls[0], /earth_day\.jpg/);
  })
);

test('createEarthTextures: .src ends with the expected filenames (default basePath)', () => {
  const gl = makeFakeGl();
  const imageFactory = makeFakeImageFactory();
  const canvasFactory = makeFakeCanvasFactory();
  createEarthTextures(gl, { imageFactory, canvasFactory });

  assert.ok(findImage(imageFactory, 'earth_day.jpg'));
  assert.ok(findImage(imageFactory, 'earth_night.jpg'));
  // Default basePath is resolved relative to textures.js (frontend/js/), not
  // this test file, so it should land on frontend/assets/.
  const dayImg = findImage(imageFactory, 'earth_day.jpg');
  assert.ok(dayImg.src.endsWith('/frontend/assets/earth_day.jpg'), dayImg.src);
});

test('createEarthTextures: custom basePath option is respected', () => {
  const gl = makeFakeGl();
  const imageFactory = makeFakeImageFactory();
  const canvasFactory = makeFakeCanvasFactory();
  createEarthTextures(gl, { imageFactory, canvasFactory, basePath: '/custom/path/' });

  const dayImg = findImage(imageFactory, 'earth_day.jpg');
  const nightImg = findImage(imageFactory, 'earth_night.jpg');
  assert.equal(dayImg.src, '/custom/path/earth_day.jpg');
  assert.equal(nightImg.src, '/custom/path/earth_night.jpg');
});

test('createEarthTextures: opts.maxSize is further clamped by gl.MAX_TEXTURE_SIZE', () => {
  const gl = makeFakeGl({ maxTextureSize: 1024 });
  const imageFactory = makeFakeImageFactory();
  const canvasFactory = makeFakeCanvasFactory();
  const handle = createEarthTextures(gl, { imageFactory, canvasFactory, maxSize: 4096 });

  const dayImg = findImage(imageFactory, 'earth_day.jpg');
  dayImg.naturalWidth = 5400;
  dayImg.naturalHeight = 2700;
  dayImg.onload();

  assert.notEqual(handle.day, null);
  const [, , , , , source] = gl.calls.texImage2D[0];
  assert.equal(source.width, 1024);
  assert.equal(source.height, 512);
});

test('createEarthTextures: anisotropic filtering applied (capped at 4x) when extension present', () => {
  const extension = { MAX_TEXTURE_MAX_ANISOTROPY_EXT: 'MAX_ANISO', TEXTURE_MAX_ANISOTROPY_EXT: 'ANISO', supported: 16 };
  const gl = makeFakeGl({ extension });
  const imageFactory = makeFakeImageFactory();
  const canvasFactory = makeFakeCanvasFactory();
  const handle = createEarthTextures(gl, { imageFactory, canvasFactory });

  const dayImg = findImage(imageFactory, 'earth_day.jpg');
  dayImg.naturalWidth = 5400;
  dayImg.naturalHeight = 2700;
  dayImg.onload();

  assert.notEqual(handle.day, null);
  assert.ok(
    gl.calls.texParameteri.some(([p, v]) => p === extension.TEXTURE_MAX_ANISOTROPY_EXT && v === 4),
    'anisotropy clamped to 4 even though 16 is supported'
  );
});

test('createEarthTextures: no anisotropy call when extension absent (default stub)', () => {
  const gl = makeFakeGl(); // extension: null
  const imageFactory = makeFakeImageFactory();
  const canvasFactory = makeFakeCanvasFactory();
  const handle = createEarthTextures(gl, { imageFactory, canvasFactory });

  const dayImg = findImage(imageFactory, 'earth_day.jpg');
  dayImg.naturalWidth = 5400;
  dayImg.naturalHeight = 2700;
  dayImg.onload();

  assert.notEqual(handle.day, null);
  // Every recorded pname should be one of the four standard params; nothing
  // anisotropy-related should have been recorded.
  const standard = new Set([gl.TEXTURE_MIN_FILTER, gl.TEXTURE_MAG_FILTER, gl.TEXTURE_WRAP_S, gl.TEXTURE_WRAP_T]);
  assert.ok(gl.calls.texParameteri.every(([p]) => standard.has(p)));
});

// ---------------------------------------------------------------------------
// Import-safety
// ---------------------------------------------------------------------------

test('module import is DOM-free (this test file itself runs under plain node)', () => {
  assert.equal(typeof potSizeFor, 'function');
  assert.equal(typeof createEarthTextures, 'function');
  assert.equal(typeof document, 'undefined');
  assert.equal(typeof window, 'undefined');
});
