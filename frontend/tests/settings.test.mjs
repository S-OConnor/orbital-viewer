import test from 'node:test';
import assert from 'node:assert/strict';
import { createSettings } from '../js/settings.js';

class FakeStorage {
  constructor(initial) {
    this.data = new Map(Object.entries(initial || {}));
  }
  getItem(key) {
    return this.data.has(key) ? this.data.get(key) : null;
  }
  setItem(key, value) {
    this.data.set(key, String(value));
  }
}

const DEFAULTS = {
  showTrails: false,
  trailSeconds: 30,
  showLabels: false,
  showSky: true,
  viewMode: 'orbit',
  categories: {
    debris: true, star: true, comet: true, satellite: true, groundHot: true, unknown: true,
  },
  host: '',
  port: 8765,
};

test('defaults are returned when storage is empty', () => {
  const storage = new FakeStorage();
  const settings = createSettings(storage);
  assert.deepEqual(settings.get(), DEFAULTS);
});

test('corrupt JSON in storage falls back to defaults', () => {
  const storage = new FakeStorage({ 'olv.settings.v1': '{not json' });
  const settings = createSettings(storage);
  assert.deepEqual(settings.get(), DEFAULTS);
});

test('missing storage key falls back to defaults without throwing', () => {
  const storage = new FakeStorage({ 'other.key': '"whatever"' });
  const settings = createSettings(storage);
  assert.deepEqual(settings.get(), DEFAULTS);
});

test('update persists to storage', () => {
  const storage = new FakeStorage();
  const settings = createSettings(storage);
  settings.update({ showTrails: true });
  const raw = storage.getItem('olv.settings.v1');
  assert.ok(raw);
  const parsed = JSON.parse(raw);
  assert.equal(parsed.showTrails, true);
});

test('trailSeconds is coerced to integer and clamped 0..60', () => {
  const settings = createSettings(new FakeStorage());
  assert.equal(settings.update({ trailSeconds: -5 }).trailSeconds, 0);
  assert.equal(settings.update({ trailSeconds: 61 }).trailSeconds, 60);
  assert.equal(settings.update({ trailSeconds: 12.7 }).trailSeconds, 12);
});

test('port is coerced to integer and clamped 1..65535', () => {
  const settings = createSettings(new FakeStorage());
  assert.equal(settings.update({ port: 0 }).port, 1);
  assert.equal(settings.update({ port: -100 }).port, 1);
  assert.equal(settings.update({ port: 70000 }).port, 65535);
  assert.equal(settings.update({ port: 9000.9 }).port, 9000);
});

test('viewMode defaults to orbit', () => {
  const settings = createSettings(new FakeStorage());
  assert.equal(settings.get().viewMode, 'orbit');
});

test('viewMode update to sat sticks and persists', () => {
  const storage = new FakeStorage();
  const settings = createSettings(storage);
  const result = settings.update({ viewMode: 'sat' });
  assert.equal(result.viewMode, 'sat');
  const raw = JSON.parse(storage.getItem('olv.settings.v1'));
  assert.equal(raw.viewMode, 'sat');
});

test('viewMode rejects anything other than exactly "orbit" or "sat"', () => {
  const settings = createSettings(new FakeStorage());
  settings.update({ viewMode: 'sat' });
  assert.equal(settings.update({ viewMode: 'SAT' }).viewMode, 'sat');
  assert.equal(settings.update({ viewMode: '' }).viewMode, 'sat');
  assert.equal(settings.update({ viewMode: null }).viewMode, 'sat');
  assert.equal(settings.update({ viewMode: 42 }).viewMode, 'sat');
  assert.equal(settings.update({ viewMode: {} }).viewMode, 'sat');
});

test('resetDefaults restores viewMode to orbit', () => {
  const settings = createSettings(new FakeStorage());
  settings.update({ viewMode: 'sat' });
  const result = settings.resetDefaults();
  assert.equal(result.viewMode, 'orbit');
});

test('stored JSON lacking viewMode loads with default orbit', () => {
  const storage = new FakeStorage({
    'olv.settings.v1': JSON.stringify({ showTrails: true }),
  });
  const settings = createSettings(storage);
  assert.equal(settings.get().viewMode, 'orbit');
});

test('onChange fires with the new viewMode value', () => {
  const settings = createSettings(new FakeStorage());
  const seen = [];
  settings.onChange((s) => seen.push(s));
  settings.update({ viewMode: 'sat' });
  assert.equal(seen.length, 1);
  assert.equal(seen[0].viewMode, 'sat');
});

test('unknown keys in patch are ignored', () => {
  const settings = createSettings(new FakeStorage());
  const result = settings.update({ bogus: 'value', showTrails: true });
  assert.equal(result.showTrails, true);
  assert.equal(result.bogus, undefined);
});

test('category merge keeps other category keys intact', () => {
  const settings = createSettings(new FakeStorage());
  const result = settings.update({ categories: { debris: false } });
  assert.deepEqual(result.categories, {
    debris: false,
    star: true,
    comet: true,
    satellite: true,
    groundHot: true,
    unknown: true,
  });
});

test('onChange fires after every applied update with the new settings', () => {
  const settings = createSettings(new FakeStorage());
  const seen = [];
  settings.onChange((s) => seen.push(s));
  settings.update({ showLabels: true });
  settings.update({ trailSeconds: 5 });
  assert.equal(seen.length, 2);
  assert.equal(seen[0].showLabels, true);
  assert.equal(seen[1].trailSeconds, 5);
});

test('get() returns independent copies', () => {
  const settings = createSettings(new FakeStorage());
  const a = settings.get();
  a.showTrails = true;
  a.categories.debris = false;
  const b = settings.get();
  assert.equal(b.showTrails, false);
  assert.equal(b.categories.debris, true);
});

test('resetDefaults restores and persists defaults', () => {
  const storage = new FakeStorage();
  const settings = createSettings(storage);
  settings.update({ showTrails: true, port: 1234 });
  const result = settings.resetDefaults();
  assert.deepEqual(result, DEFAULTS);
  const raw = JSON.parse(storage.getItem('olv.settings.v1'));
  assert.deepEqual(raw, DEFAULTS);
});

test('update with non-object patch is a no-op', () => {
  const settings = createSettings(new FakeStorage());
  const before = settings.get();
  const after = settings.update(null);
  assert.deepEqual(after, before);
});

test('siteDefaults override built-ins when storage is empty', () => {
  const settings = createSettings(new FakeStorage(), { port: 9000 });
  assert.equal(settings.get().port, 9000);
  // Untouched keys still fall back to the built-in defaults.
  assert.equal(settings.get().host, DEFAULTS.host);
  assert.equal(settings.get().trailSeconds, DEFAULTS.trailSeconds);
});

test('stored user settings beat siteDefaults', () => {
  const storage = new FakeStorage({
    'olv.settings.v1': JSON.stringify({ port: 1234 }),
  });
  const settings = createSettings(storage, { port: 9000 });
  assert.equal(settings.get().port, 1234);
});

test('resetDefaults returns to site-merged defaults and persists them', () => {
  const storage = new FakeStorage();
  const settings = createSettings(storage, { port: 9000, showTrails: true });
  settings.update({ port: 1234, showTrails: false });
  const result = settings.resetDefaults();
  assert.equal(result.port, 9000);
  assert.equal(result.showTrails, true);
  const raw = JSON.parse(storage.getItem('olv.settings.v1'));
  assert.equal(raw.port, 9000);
  assert.equal(raw.showTrails, true);
});

test('out-of-range siteDefaults are clamped (trailSeconds 99 -> 60)', () => {
  const settings = createSettings(new FakeStorage(), { trailSeconds: 99, port: -5 });
  assert.equal(settings.get().trailSeconds, 60);
  assert.equal(settings.get().port, 1);
});

test('unknown siteDefaults keys are ignored', () => {
  const settings = createSettings(new FakeStorage(), { bogus: 'value', port: 9000 });
  const result = settings.get();
  assert.equal(result.port, 9000);
  assert.equal(result.bogus, undefined);
});

test('siteDefaults defaults to {} when omitted (backward compatible)', () => {
  const settings = createSettings(new FakeStorage());
  assert.deepEqual(settings.get(), DEFAULTS);
});

test('showSky defaults to true', () => {
  const settings = createSettings(new FakeStorage());
  assert.equal(settings.get().showSky, true);
});

test('showSky update persists and can be turned off', () => {
  const storage = new FakeStorage();
  const settings = createSettings(storage);
  const result = settings.update({ showSky: false });
  assert.equal(result.showSky, false);
  const raw = JSON.parse(storage.getItem('olv.settings.v1'));
  assert.equal(raw.showSky, false);
});

test('showSky patch values are coerced via Boolean()', () => {
  const settings = createSettings(new FakeStorage());
  assert.equal(settings.update({ showSky: 0 }).showSky, false);
  assert.equal(settings.update({ showSky: 'x' }).showSky, true);
});

test('resetDefaults restores showSky to true', () => {
  const settings = createSettings(new FakeStorage());
  settings.update({ showSky: false });
  const result = settings.resetDefaults();
  assert.equal(result.showSky, true);
});

test('siteDefaults showSky flows through', () => {
  const settings = createSettings(new FakeStorage(), { showSky: false });
  assert.equal(settings.get().showSky, false);
});
