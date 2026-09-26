// net.js — WebSocket JSON protocol parsing + connection management.
//
// Pure parsing logic (parseStateMessage) must not touch window/document/
// WebSocket at import time — it is imported directly by Node tests and by
// frontend/tests/validate_message.mjs. createConnection() only references
// the global WebSocket constructor lazily, inside connect(), and only if
// no wsFactory override was supplied (test injection point).
//
// Wire format: docs/PROTOCOL_WS.md (normative).

// Any integer category not listed here (a newer or confused sender) parses as
// 'unknown' rather than failing the whole frame.
const CATEGORY_NAMES = {
  0: 'unknown',
  1: 'debris',
  2: 'star',
  3: 'comet',
  4: 'satellite',
  5: 'groundHot',
};

function isFiniteNumber(v) {
  return typeof v === 'number' && Number.isFinite(v);
}

function isNumberArray3(v) {
  return Array.isArray(v) && v.length === 3 && v.every(isFiniteNumber);
}

function isPlainObject(v) {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

/**
 * parseStateMessage(text) — pure. Parses one WS text frame per
 * docs/PROTOCOL_WS.md. Throws Error with a specific message on any
 * violation of the protocol (invalid JSON, missing/unknown type, malformed
 * fields). Unknown `type` values are NOT silently ignored here — callers
 * (createConnection) are responsible for treating parse failures as
 * recoverable (log + count + keep connection alive); see PROTOCOL_WS.md §3.
 */
export function parseStateMessage(text) {
  const msg = JSON.parse(text);
  if (!isPlainObject(msg)) {
    throw new Error('parseStateMessage: message must be a JSON object');
  }
  if (typeof msg.type !== 'string') {
    throw new Error('parseStateMessage: missing "type" field');
  }
  if (msg.type === 'hello') {
    return parseHello(msg);
  }
  if (msg.type === 'state') {
    return parseState(msg);
  }
  throw new Error(`parseStateMessage: unknown message type "${msg.type}"`);
}

function parseHello(msg) {
  if (msg.protocolVersion !== 1) {
    throw new Error(`parseStateMessage: unsupported protocolVersion ${msg.protocolVersion}`);
  }
  if (typeof msg.serverTime !== 'string') {
    throw new Error('parseStateMessage: hello.serverTime must be a string');
  }
  if (!isFiniteNumber(msg.broadcastHz)) {
    throw new Error('parseStateMessage: hello.broadcastHz must be a number');
  }
  if (!isPlainObject(msg.limits)) {
    throw new Error('parseStateMessage: hello.limits must be an object');
  }
  return {
    type: 'hello',
    protocolVersion: msg.protocolVersion,
    serverTime: msg.serverTime,
    broadcastHz: msg.broadcastHz,
    limits: msg.limits,
  };
}

function parseState(msg) {
  if (typeof msg.serverTime !== 'string') {
    throw new Error('parseStateMessage: state.serverTime must be a string');
  }
  if (msg.lastDataTime !== null && typeof msg.lastDataTime !== 'string') {
    throw new Error('parseStateMessage: state.lastDataTime must be a string or null');
  }
  const satellite = parseSatellite(msg.satellite);
  if (!Array.isArray(msg.objects)) {
    throw new Error('parseStateMessage: state.objects must be an array');
  }
  const objects = msg.objects.map(parseObjectRow);
  if (!isPlainObject(msg.stats)) {
    throw new Error('parseStateMessage: state.stats must be an object');
  }
  return {
    type: 'state',
    serverTime: msg.serverTime,
    lastDataTime: msg.lastDataTime,
    satellite,
    objects,
    stats: msg.stats,
  };
}

function parseSatellite(sat) {
  if (sat === null || sat === undefined) return null;
  if (!isPlainObject(sat)) {
    throw new Error('parseStateMessage: satellite must be an object or null');
  }
  if (!isFiniteNumber(sat.id) || !isFiniteNumber(sat.seq)) {
    throw new Error('parseStateMessage: satellite.id/seq must be numbers');
  }
  if (!isNumberArray3(sat.pos)) {
    throw new Error('parseStateMessage: satellite.pos must be an array of 3 finite numbers');
  }
  if (!isNumberArray3(sat.vel)) {
    throw new Error('parseStateMessage: satellite.vel must be an array of 3 finite numbers');
  }
  return { id: sat.id, seq: sat.seq, pos: sat.pos.slice(), vel: sat.vel.slice() };
}

function parseObjectRow(row, index) {
  if (!Array.isArray(row) || row.length !== 11) {
    throw new Error(`parseStateMessage: objects[${index}] must be an 11-element array`);
  }
  const [id, cat, px, py, pz, vx, vy, vz, conf, intensity, flags] = row;
  if (!isFiniteNumber(id)) {
    throw new Error(`parseStateMessage: objects[${index}].id must be a number`);
  }
  if (!Number.isInteger(cat)) {
    throw new Error(`parseStateMessage: objects[${index}].cat must be an integer`);
  }
  const catName = CATEGORY_NAMES[cat] || 'unknown';
  if (![px, py, pz].every(isFiniteNumber)) {
    throw new Error(`parseStateMessage: objects[${index}] position must be finite numbers`);
  }
  let vel;
  if (vx === null) {
    if (vy !== null || vz !== null) {
      throw new Error(`parseStateMessage: objects[${index}] velocity must be all-null or all-numeric`);
    }
    vel = null;
  } else {
    if (![vx, vy, vz].every(isFiniteNumber)) {
      throw new Error(`parseStateMessage: objects[${index}] velocity must be finite numbers or null`);
    }
    vel = [vx, vy, vz];
  }
  if (!isFiniteNumber(conf)) {
    throw new Error(`parseStateMessage: objects[${index}].conf must be a number`);
  }
  if (!isFiniteNumber(intensity)) {
    throw new Error(`parseStateMessage: objects[${index}].intensity must be a number`);
  }
  if (!isFiniteNumber(flags)) {
    throw new Error(`parseStateMessage: objects[${index}].flags must be a number`);
  }
  return { id, cat: catName, pos: [px, py, pz], vel, conf, intensity, flags };
}

const BACKOFF_DELAYS_MS = [1000, 2000, 4000, 8000, 10000];

/**
 * createConnection({host, port, onState, onHello, onStatus, wsFactory})
 *   → {connect(), close(), setEndpoint(host, port)}
 *
 * wsFactory is an additive, optional extension beyond the PLAN.md §6
 * summary signature: it lets tests inject a fake WebSocket-like object
 * instead of touching the global WebSocket constructor. When omitted, the
 * global `WebSocket` is used, and only ever referenced lazily inside
 * connect()/openSocket() — never at module import time.
 */
export function createConnection({ host, port, onState, onHello, onStatus, wsFactory } = {}) {
  let _host = host;
  let _port = port;
  let socket = null;
  let reconnectTimer = null;
  let backoffIndex = 0;
  let stopped = true;
  let parseErrorCount = 0;

  const factory = typeof wsFactory === 'function' ? wsFactory : (url) => new WebSocket(url);

  function emitStatus(status) {
    if (typeof onStatus === 'function') onStatus(status);
  }

  function scheduleReconnect() {
    if (stopped) return;
    const delay = BACKOFF_DELAYS_MS[Math.min(backoffIndex, BACKOFF_DELAYS_MS.length - 1)];
    backoffIndex += 1;
    reconnectTimer = setTimeout(() => {
      reconnectTimer = null;
      openSocket();
    }, delay);
  }

  function openSocket() {
    if (stopped) return;
    emitStatus('connecting');
    const url = `ws://${_host}:${_port}/`;
    let ws;
    try {
      ws = factory(url);
    } catch (err) {
      console.warn('createConnection: failed to open WebSocket', err);
      scheduleReconnect();
      return;
    }
    socket = ws;
    ws.onopen = () => {
      if (socket !== ws) return;
      backoffIndex = 0;
      emitStatus('open');
    };
    ws.onmessage = (evt) => {
      if (socket !== ws) return;
      let parsed;
      try {
        parsed = parseStateMessage(evt.data);
      } catch (err) {
        parseErrorCount += 1;
        console.warn('createConnection: parse error', err && err.message ? err.message : err);
        return;
      }
      if (parsed.type === 'hello') {
        if (typeof onHello === 'function') onHello(parsed);
      } else if (parsed.type === 'state') {
        if (typeof onState === 'function') onState(parsed);
      }
    };
    ws.onclose = () => {
      if (socket !== ws) return;
      socket = null;
      emitStatus('closed');
      scheduleReconnect();
    };
    ws.onerror = () => {
      // onclose normally follows; nothing additional required here.
    };
  }

  return {
    connect() {
      stopped = false;
      backoffIndex = 0;
      if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
      }
      openSocket();
    },
    close() {
      stopped = true;
      if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
      }
      if (socket) {
        const s = socket;
        socket = null;
        try {
          s.close();
        } catch (err) {
          // ignore
        }
      }
    },
    setEndpoint(newHost, newPort) {
      _host = newHost;
      _port = newPort;
      if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
      }
      if (socket) {
        const s = socket;
        socket = null;
        try {
          s.close();
        } catch (err) {
          // ignore
        }
      }
      backoffIndex = 0;
      stopped = false;
      openSocket();
    },
    getParseErrorCount() {
      return parseErrorCount;
    },
  };
}
