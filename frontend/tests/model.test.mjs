import test from 'node:test';
import assert from 'node:assert/strict';
import { createModel } from '../js/model.js';

function sampleMsg() {
  return {
    type: 'state',
    serverTime: '2026-07-01T12:00:01.000Z',
    lastDataTime: '2026-07-01T12:00:00.500Z',
    satellite: { id: 1, seq: 5, pos: [1, 2, 3], vel: [4, 5, 6] },
    objects: [
      { id: 20, cat: 'star', pos: [1, 1, 1], vel: null, conf: 90, intensity: 2.0, flags: 0 },
      { id: 10, cat: 'debris', pos: [2, 2, 2], vel: [1, 1, 1], conf: 50, intensity: 0, flags: 0 },
      { id: 30, cat: 'debris', pos: [3, 3, 3], vel: null, conf: 10, intensity: 0, flags: 2 },
    ],
    stats: {
      udpReceived: 10,
      udpAccepted: 9,
      udpDropped: 1,
      udpRateHz: 2.0,
      wsClients: 1,
      objectCount: 3,
      broadcastSeq: 4,
    },
  };
}

test('applyState then getSnapshot matches the renderer snap shape exactly', () => {
  const model = createModel();
  const msg = sampleMsg();
  model.applyState(msg, 1000);
  assert.deepEqual(model.getSnapshot(), {
    satellite: msg.satellite,
    objects: msg.objects,
    lastDataTime: msg.lastDataTime,
    serverTime: msg.serverTime,
    trailPoints: [],
  });
});

test('getSnapshot before any applyState is empty/null', () => {
  const model = createModel();
  assert.deepEqual(model.getSnapshot(), {
    satellite: null,
    objects: [],
    lastDataTime: null,
    serverTime: null,
    trailPoints: [],
  });
});

test('getSnapshot exposes trailPoints from a state message', () => {
  const model = createModel();
  const msg = sampleMsg();
  msg.trailPoints = [
    { id: 2001, t: 1790000000.125, pos: [6923371.4, 12000.0, -55000.2] },
    { id: 2001, t: 1790000000.225, pos: [6923380.9, 12011.5, -55001.0] },
  ];
  model.applyState(msg, 1000);
  assert.deepEqual(model.getSnapshot().trailPoints, msg.trailPoints);
});

test('getSnapshot: trailPoints is replaced, not accumulated, by the next applyState', () => {
  const model = createModel();
  const first = sampleMsg();
  first.trailPoints = [{ id: 2001, t: 1, pos: [1, 2, 3] }];
  model.applyState(first, 1000);
  assert.equal(model.getSnapshot().trailPoints.length, 1);

  const second = sampleMsg();
  second.trailPoints = [
    { id: 2001, t: 2, pos: [4, 5, 6] },
    { id: 2001, t: 3, pos: [7, 8, 9] },
  ];
  model.applyState(second, 2000);
  assert.deepEqual(model.getSnapshot().trailPoints, second.trailPoints);

  const third = sampleMsg();
  delete third.trailPoints;
  model.applyState(third, 3000);
  assert.deepEqual(model.getSnapshot().trailPoints, []);
});

test('getSnapshot: serverTime passthrough from a state message', () => {
  const model = createModel();
  const msg = sampleMsg();
  msg.serverTime = '2026-07-01T12:34:56.789Z';
  model.applyState(msg, 1000);
  assert.equal(model.getSnapshot().serverTime, '2026-07-01T12:34:56.789Z');
});

test('getSnapshot: serverTime is null when the state message omits it', () => {
  const model = createModel();
  const msg = sampleMsg();
  delete msg.serverTime;
  model.applyState(msg, 1000);
  assert.equal(model.getSnapshot().serverTime, null);
});

test('getCounts tallies by category plus total', () => {
  const model = createModel();
  model.applyState(sampleMsg(), 1000);
  assert.deepEqual(model.getCounts(), {
    debris: 2,
    star: 1,
    comet: 0,
    satellite: 0,
    groundHot: 0,
    unknown: 0,
    total: 3,
  });
});

test('getListRows: no filter returns all rows sorted by id ascending', () => {
  const model = createModel();
  model.applyState(sampleMsg(), 1000);
  const { rows, total } = model.getListRows(null, 1000);
  assert.equal(total, 3);
  assert.deepEqual(rows.map((r) => r.id), [10, 20, 30]);
});

test('getListRows: filter by category', () => {
  const model = createModel();
  model.applyState(sampleMsg(), 1000);
  const { rows, total } = model.getListRows('debris', 1000);
  assert.equal(total, 2);
  assert.deepEqual(rows.map((r) => r.id), [10, 30]);
});

test('getListRows: cap limits rows returned but total reflects filtered count', () => {
  const model = createModel();
  model.applyState(sampleMsg(), 1000);
  const { rows, total } = model.getListRows(null, 1);
  assert.equal(rows.length, 1);
  assert.equal(rows[0].id, 10);
  assert.equal(total, 3);
});

test('getListRows default cap is 1000', () => {
  const model = createModel();
  model.applyState(sampleMsg(), 1000);
  const { rows } = model.getListRows(null);
  assert.equal(rows.length, 3);
});

test('getLastDataTime passthrough', () => {
  const model = createModel();
  model.applyState(sampleMsg(), 1000);
  assert.equal(model.getLastDataTime(), '2026-07-01T12:00:00.500Z');
});

test('secondsSinceLastState derives from client-clock nowMs stamps', () => {
  const model = createModel();
  model.applyState(sampleMsg(), 1000);
  assert.equal(model.secondsSinceLastState(4000), 3);
  assert.equal(model.secondsSinceLastState(1500), 0.5);
});

test('secondsSinceLastState is Infinity before any state applied', () => {
  const model = createModel();
  assert.equal(model.secondsSinceLastState(1000), Infinity);
});

test('getStats exposes the last stats object, null before first applyState', () => {
  const model = createModel();
  assert.equal(model.getStats(), null);
  const msg = sampleMsg();
  model.applyState(msg, 1000);
  assert.deepEqual(model.getStats(), msg.stats);
});

test('applyState ignores non-state messages', () => {
  const model = createModel();
  model.applyState(sampleMsg(), 1000);
  model.applyState({ type: 'hello', protocolVersion: 1 }, 5000);
  // state from the state message should be untouched
  assert.equal(model.getCounts().total, 3);
  assert.equal(model.secondsSinceLastState(4000), 3);
});
