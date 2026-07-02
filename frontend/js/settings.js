// settings.js — persisted UI/renderer settings store.
//
// Pure logic module: no window/document access. `storage` is injected
// ({getItem, setItem}) so this can be unit-tested with a fake and used in
// the browser with window.localStorage.

const STORAGE_KEY = 'olv.settings.v1';

const DEFAULTS = {
  showTrails: false,
  trailSeconds: 30,
  showLabels: false,
  categories: {
    debris: true,
    star: true,
    comet: true,
    satellite: true,
    groundHot: true,
  },
  host: '',
  port: 8765,
};

function clone(value) {
  return JSON.parse(JSON.stringify(value));
}

function clampInt(value, min, max, fallback) {
  const n = Number(value);
  if (!Number.isFinite(n)) return fallback;
  const truncated = Math.trunc(n);
  return Math.min(max, Math.max(min, truncated));
}

function loadStored(storage) {
  if (!storage || typeof storage.getItem !== 'function') return null;
  let raw;
  try {
    raw = storage.getItem(STORAGE_KEY);
  } catch (err) {
    return null;
  }
  if (!raw) return null;
  try {
    const parsed = JSON.parse(raw);
    if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) return null;
    return parsed;
  } catch (err) {
    return null;
  }
}

function applyPatch(base, patch) {
  if ('showTrails' in patch) base.showTrails = Boolean(patch.showTrails);
  if ('showLabels' in patch) base.showLabels = Boolean(patch.showLabels);
  if ('trailSeconds' in patch) {
    base.trailSeconds = clampInt(patch.trailSeconds, 0, 60, base.trailSeconds);
  }
  if ('port' in patch) {
    base.port = clampInt(patch.port, 1, 65535, base.port);
  }
  if ('host' in patch && typeof patch.host === 'string') {
    base.host = patch.host;
  }
  if ('categories' in patch && patch.categories && typeof patch.categories === 'object' && !Array.isArray(patch.categories)) {
    for (const key of Object.keys(DEFAULTS.categories)) {
      if (key in patch.categories) {
        base.categories[key] = Boolean(patch.categories[key]);
      }
    }
  }
  return base;
}

function normalize(raw, base) {
  const result = clone(base);
  if (raw && typeof raw === 'object') {
    applyPatch(result, raw);
  }
  return result;
}

// Recognized siteDefaults keys, same set update()/applyPatch() accept (minus
// `categories`, which config.toml does not configure). siteDefaults values
// are merged over the built-in DEFAULTS using the same clamping rules as
// update(), so an out-of-range site value (e.g. trailSeconds: 99) clamps
// exactly like a user update would rather than propagating invalid state.
function mergeSiteDefaults(siteDefaults) {
  const merged = clone(DEFAULTS);
  if (siteDefaults && typeof siteDefaults === 'object') {
    applyPatch(merged, siteDefaults);
  }
  return merged;
}

/**
 * createSettings(storage, siteDefaults = {}) → {get(), update(patch), onChange(cb), resetDefaults()}
 *
 * Effective defaults are the built-in DEFAULTS with siteDefaults' recognized
 * keys (host, port, showTrails, trailSeconds, showLabels — loaded from
 * config.toml via site_config.js) merged over them; stored user settings
 * (localStorage) still load on top of that, so a user's own choices always
 * win over site defaults. resetDefaults() resets to this merged baseline.
 */
export function createSettings(storage, siteDefaults = {}) {
  const effectiveDefaults = mergeSiteDefaults(siteDefaults);
  let current = normalize(loadStored(storage), effectiveDefaults);
  const listeners = new Set();

  function persist() {
    if (!storage || typeof storage.setItem !== 'function') return;
    try {
      storage.setItem(STORAGE_KEY, JSON.stringify(current));
    } catch (err) {
      // tolerate storage errors (quota, disabled, etc.)
    }
  }

  function notify() {
    const snapshot = clone(current);
    for (const cb of listeners) {
      cb(snapshot);
    }
  }

  function get() {
    return clone(current);
  }

  function update(patch) {
    if (patch && typeof patch === 'object' && !Array.isArray(patch)) {
      current = applyPatch(clone(current), patch);
      persist();
      notify();
    }
    return get();
  }

  function onChange(cb) {
    if (typeof cb !== 'function') return () => {};
    listeners.add(cb);
    return () => listeners.delete(cb);
  }

  function resetDefaults() {
    current = clone(effectiveDefaults);
    persist();
    notify();
    return get();
  }

  return { get, update, onChange, resetDefaults };
}
