import test from 'node:test';
import assert from 'node:assert/strict';
import { loadSiteConfig } from '../js/site_config.js';

function fetchOk(text) {
  return async () => ({ ok: true, text: async () => text });
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

test('fetch throws -> {}', async () => {
  const result = await loadSiteConfig(async () => {
    throw new Error('network down');
  });
  assert.deepEqual(result, {});
});

test('resp.ok is false -> {}', async () => {
  const result = await loadSiteConfig(async () => ({ ok: false, text: async () => '' }));
  assert.deepEqual(result, {});
});

test('valid full config -> all five keys mapped', async () => {
  const text = [
    '[websocket]',
    'host = "sat.example.com"',
    'port = 9000',
    '',
    '[display]',
    'show_trails = true',
    'trail_seconds = 45',
    'show_labels = true',
  ].join('\n');
  const result = await loadSiteConfig(fetchOk(text));
  assert.deepEqual(result, {
    host: 'sat.example.com',
    port: 9000,
    showTrails: true,
    trailSeconds: 45,
    showLabels: true,
  });
});

test(
  'TOML syntax error -> {} and console.warn is called',
  withStubbedWarn(async (calls) => {
    const result = await loadSiteConfig(fetchOk('key value\n'));
    assert.deepEqual(result, {});
    assert.equal(calls.length, 1);
    assert.match(calls[0], /^config\.toml ignored: line 1: /);
  })
);

test(
  'unknown key is skipped but valid siblings are kept',
  withStubbedWarn(async (calls) => {
    const text = '[websocket]\nport = 9001\nbogus = "x"\n';
    const result = await loadSiteConfig(fetchOk(text));
    assert.deepEqual(result, { port: 9001 });
    assert.equal(calls.length, 1);
    assert.match(calls[0], /unknown key "websocket\.bogus"/);
  })
);

test(
  'port out of range (70000) is skipped',
  withStubbedWarn(async (calls) => {
    const text = '[websocket]\nport = 70000\nhost = "ok.example.com"\n';
    const result = await loadSiteConfig(fetchOk(text));
    assert.deepEqual(result, { host: 'ok.example.com' });
    assert.equal(calls.length, 1);
    assert.match(calls[0], /invalid value for key "websocket\.port"/);
  })
);

test(
  'trail_seconds out of range (61) is skipped',
  withStubbedWarn(async (calls) => {
    const text = '[display]\ntrail_seconds = 61\nshow_labels = true\n';
    const result = await loadSiteConfig(fetchOk(text));
    assert.deepEqual(result, { showLabels: true });
    assert.equal(calls.length, 1);
    assert.match(calls[0], /invalid value for key "display\.trail_seconds"/);
  })
);

test(
  'show_trails as integer (1) is skipped (wrong type)',
  withStubbedWarn(async (calls) => {
    const text = '[display]\nshow_trails = 1\n';
    const result = await loadSiteConfig(fetchOk(text));
    assert.deepEqual(result, {});
    assert.equal(calls.length, 1);
    assert.match(calls[0], /invalid value for key "display\.show_trails"/);
  })
);

test(
  'host as non-string (integer) is skipped (wrong type)',
  withStubbedWarn(async (calls) => {
    const text = '[websocket]\nhost = 12345\n';
    const result = await loadSiteConfig(fetchOk(text));
    assert.deepEqual(result, {});
    assert.equal(calls.length, 1);
    assert.match(calls[0], /invalid value for key "websocket\.host"/);
  })
);

test('missing config.toml is silent: no console.warn call', async () => {
  const calls = [];
  const original = console.warn;
  console.warn = (...args) => calls.push(args.join(' '));
  try {
    const result = await loadSiteConfig(async () => ({ ok: false, text: async () => '' }));
    assert.deepEqual(result, {});
    assert.equal(calls.length, 0);
  } finally {
    console.warn = original;
  }
});
