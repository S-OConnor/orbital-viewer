import test from 'node:test';
import assert from 'node:assert/strict';
import { parseStateMessage, createConnection } from '../js/net.js';

function sampleStateObj() {
  return {
    type: 'state',
    serverTime: '2026-07-01T12:34:56.789Z',
    lastDataTime: '2026-07-01T12:34:56.500Z',
    satellite: {
      id: 1,
      seq: 42,
      pos: [4126540.2, -4681712.5, 1003432.1],
      vel: [1234.56, 2345.67, -6543.21],
    },
    objects: [
      [1001, 1, 6923371.4, 12000.0, -55000.2, 7611.0, 0.0, 0.0, 87, 0.0, 0],
      [2001, 5, 1113194.9, -4842330.0, 3985029.2, null, null, null, 95, 1450.0, 1],
    ],
    stats: {
      udpReceived: 480,
      udpAccepted: 478,
      udpDropped: 2,
      udpRateHz: 8.0,
      wsClients: 1,
      objectCount: 2,
      broadcastSeq: 12,
    },
  };
}

test('parses a valid state message: expands rows and maps categories', () => {
  const parsed = parseStateMessage(JSON.stringify(sampleStateObj()));
  assert.equal(parsed.type, 'state');
  assert.equal(parsed.serverTime, '2026-07-01T12:34:56.789Z');
  assert.equal(parsed.lastDataTime, '2026-07-01T12:34:56.500Z');
  assert.deepEqual(parsed.satellite, {
    id: 1,
    seq: 42,
    pos: [4126540.2, -4681712.5, 1003432.1],
    vel: [1234.56, 2345.67, -6543.21],
  });
  assert.equal(parsed.objects.length, 2);
  assert.deepEqual(parsed.objects[0], {
    id: 1001,
    cat: 'debris',
    pos: [6923371.4, 12000.0, -55000.2],
    vel: [7611.0, 0.0, 0.0],
    conf: 87,
    intensity: 0.0,
    flags: 0,
  });
  assert.deepEqual(parsed.objects[1], {
    id: 2001,
    cat: 'groundHot',
    pos: [1113194.9, -4842330.0, 3985029.2],
    vel: null,
    conf: 95,
    intensity: 1450.0,
    flags: 1,
  });
  assert.deepEqual(parsed.stats, sampleStateObj().stats);
});

test('parses a valid state message with null satellite and empty objects', () => {
  const obj = sampleStateObj();
  obj.satellite = null;
  obj.objects = [];
  const parsed = parseStateMessage(JSON.stringify(obj));
  assert.equal(parsed.satellite, null);
  assert.deepEqual(parsed.objects, []);
});

test('parses a valid hello message', () => {
  const text = JSON.stringify({
    type: 'hello',
    protocolVersion: 1,
    serverTime: '2026-07-01T12:00:00.000Z',
    broadcastHz: 1.0,
    limits: { maxObjects: 5000 },
  });
  const parsed = parseStateMessage(text);
  assert.deepEqual(parsed, {
    type: 'hello',
    protocolVersion: 1,
    serverTime: '2026-07-01T12:00:00.000Z',
    broadcastHz: 1.0,
    limits: { maxObjects: 5000 },
  });
});

test('rejects invalid JSON', () => {
  assert.throws(() => parseStateMessage('{not json'));
});

test('rejects missing type', () => {
  assert.throws(() => parseStateMessage(JSON.stringify({ foo: 'bar' })));
});

test('rejects unknown type', () => {
  assert.throws(() => parseStateMessage(JSON.stringify({ type: 'bogus' })));
});

test('rejects object rows of length 10', () => {
  const obj = sampleStateObj();
  obj.objects = [[1, 1, 0, 0, 0, 0, 0, 0, 100, 0]];
  assert.throws(() => parseStateMessage(JSON.stringify(obj)));
});

test('rejects non-numeric id in an object row', () => {
  const obj = sampleStateObj();
  obj.objects = [['x', 1, 0, 0, 0, null, null, null, 100, 0, 0]];
  assert.throws(() => parseStateMessage(JSON.stringify(obj)));
});

test('rejects unknown category number', () => {
  const obj = sampleStateObj();
  obj.objects = [[1, 9, 0, 0, 0, null, null, null, 100, 0, 0]];
  assert.throws(() => parseStateMessage(JSON.stringify(obj)));
});

test('rejects satellite pos of length 2', () => {
  const obj = sampleStateObj();
  obj.satellite = { id: 1, seq: 1, pos: [1, 2], vel: [0, 0, 0] };
  assert.throws(() => parseStateMessage(JSON.stringify(obj)));
});

test('rejects hello with protocolVersion 2', () => {
  const text = JSON.stringify({
    type: 'hello',
    protocolVersion: 2,
    serverTime: '2026-07-01T12:00:00.000Z',
    broadcastHz: 1.0,
    limits: {},
  });
  assert.throws(() => parseStateMessage(text));
});

// ---- createConnection with an injected fake wsFactory --------------------

class FakeSocket {
  constructor(url) {
    this.url = url;
    this.readyState = 0;
    this.onopen = null;
    this.onmessage = null;
    this.onclose = null;
    this.onerror = null;
  }

  close() {
    this.readyState = 3;
    if (this.onclose) this.onclose({ code: 1000 });
  }

  // test helpers simulating server-driven events
  _open() {
    this.readyState = 1;
    if (this.onopen) this.onopen({});
  }

  _serverClose() {
    this.readyState = 3;
    if (this.onclose) this.onclose({ code: 1006 });
  }

  _message(data) {
    if (this.onmessage) this.onmessage({ data });
  }
}

function stubTimers() {
  const scheduled = [];
  const originalSetTimeout = globalThis.setTimeout;
  const originalClearTimeout = globalThis.clearTimeout;
  globalThis.setTimeout = (fn, delay) => {
    const id = scheduled.length + 1;
    scheduled.push({ id, fn, delay });
    return id;
  };
  globalThis.clearTimeout = () => {};
  return {
    scheduled,
    restore() {
      globalThis.setTimeout = originalSetTimeout;
      globalThis.clearTimeout = originalClearTimeout;
    },
  };
}

test('createConnection: status sequence connecting -> open -> closed -> reconnect scheduled', () => {
  const timers = stubTimers();
  const instances = [];
  const statuses = [];
  try {
    const conn = createConnection({
      host: '127.0.0.1',
      port: 8765,
      onState: () => {},
      onHello: () => {},
      onStatus: (s) => statuses.push(s),
      wsFactory: (url) => {
        const sock = new FakeSocket(url);
        instances.push(sock);
        return sock;
      },
    });

    conn.connect();
    assert.deepEqual(statuses, ['connecting']);
    assert.equal(instances.length, 1);
    assert.equal(instances[0].url, 'ws://127.0.0.1:8765/');

    instances[0]._open();
    assert.deepEqual(statuses, ['connecting', 'open']);

    instances[0]._serverClose();
    assert.deepEqual(statuses, ['connecting', 'open', 'closed']);
    assert.equal(timers.scheduled.length, 1);
    assert.equal(timers.scheduled[0].delay, 1000);

    // Simulate the backoff timer firing: should reconnect and, on a second
    // consecutive failure, back off to the next delay tier.
    timers.scheduled[0].fn();
    assert.deepEqual(statuses, ['connecting', 'open', 'closed', 'connecting']);
    assert.equal(instances.length, 2);

    instances[1]._serverClose();
    assert.equal(timers.scheduled.length, 2);
    assert.equal(timers.scheduled[1].delay, 2000);

    conn.close();
  } finally {
    timers.restore();
  }
});

test('createConnection: close() disables reconnection', () => {
  const timers = stubTimers();
  const instances = [];
  try {
    const conn = createConnection({
      host: '127.0.0.1',
      port: 8765,
      onState: () => {},
      onHello: () => {},
      onStatus: () => {},
      wsFactory: (url) => {
        const sock = new FakeSocket(url);
        instances.push(sock);
        return sock;
      },
    });
    conn.connect();
    instances[0]._open();
    conn.close();
    // Any late close event on the now-superseded socket must not schedule
    // a reconnect.
    instances[0]._serverClose();
    assert.equal(timers.scheduled.length, 0);
  } finally {
    timers.restore();
  }
});

test('createConnection: dispatches hello and state to the right callbacks, tolerates parse errors', () => {
  const timers = stubTimers();
  const instances = [];
  const hellos = [];
  const states = [];
  const originalWarn = console.warn;
  let warnCount = 0;
  console.warn = () => {
    warnCount += 1;
  };
  try {
    const conn = createConnection({
      host: 'h',
      port: 1,
      onState: (m) => states.push(m),
      onHello: (m) => hellos.push(m),
      onStatus: () => {},
      wsFactory: (url) => {
        const sock = new FakeSocket(url);
        instances.push(sock);
        return sock;
      },
    });
    conn.connect();
    instances[0]._open();
    instances[0]._message('not json');
    assert.equal(warnCount, 1);
    assert.equal(conn.getParseErrorCount(), 1);

    instances[0]._message(
      JSON.stringify({
        type: 'hello',
        protocolVersion: 1,
        serverTime: 't',
        broadcastHz: 1,
        limits: {},
      })
    );
    assert.equal(hellos.length, 1);

    instances[0]._message(JSON.stringify(sampleStateObj()));
    assert.equal(states.length, 1);
    assert.equal(states[0].type, 'state');

    conn.close();
  } finally {
    console.warn = originalWarn;
    timers.restore();
  }
});
