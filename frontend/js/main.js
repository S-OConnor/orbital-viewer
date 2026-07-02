// main.js — wires settings, connection, model, renderer, and ui together.
// The only other DOM-touching module is ui.js; net.js/model.js/settings.js
// stay DOM-free so they can be imported by Node tests.

import { createConnection } from './net.js';
import { createModel } from './model.js';
import { createSettings } from './settings.js';
import { createUi } from './ui.js';
import { createRenderer } from './renderer.js';
import { loadSiteConfig } from './site_config.js';

const PICK_DRAG_THRESHOLD_PX = 5;
const UI_TICK_INTERVAL_MS = 1000;

function resolveHost(settings) {
  return settings.host || window.location.hostname || '127.0.0.1';
}

function wsUrl(host, port) {
  return `ws://${host}:${port}/`;
}

function showFatalError(el, err) {
  if (!el) return;
  const message = err && err.message ? err.message : String(err);
  el.textContent = `Unable to initialize the WebGL renderer: ${message}`;
  el.classList.remove('hidden');
}

function applyRendererSettings(renderer, s) {
  renderer.setSettings({
    showTrails: s.showTrails,
    trailSeconds: s.trailSeconds,
    showLabels: s.showLabels,
    categories: s.categories,
  });
}

function wireOverlayPicking(overlayCanvas, getRenderer, getUi) {
  let downX = 0;
  let downY = 0;
  let dragging = false;

  overlayCanvas.addEventListener('pointerdown', (e) => {
    downX = e.clientX;
    downY = e.clientY;
    dragging = false;
  });
  overlayCanvas.addEventListener('pointermove', (e) => {
    if (Math.abs(e.clientX - downX) > PICK_DRAG_THRESHOLD_PX || Math.abs(e.clientY - downY) > PICK_DRAG_THRESHOLD_PX) {
      dragging = true;
    }
  });
  overlayCanvas.addEventListener('pointerup', (e) => {
    if (dragging) return;
    const renderer = getRenderer();
    const ui = getUi();
    if (!renderer || !ui) return;
    const rect = overlayCanvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    const id = renderer.pick(x, y);
    renderer.setSelected(id);
    ui.setSelected(id);
  });
}

async function loadSiteDefaults() {
  // loadSiteConfig() already resolves to {} on every failure it knows
  // about (fetch rejects, non-OK response, TOML syntax error, invalid
  // individual keys); this wrapper is an extra safety net so a startup
  // config load can never block or fail boot() for any other reason.
  try {
    return await loadSiteConfig(window.fetch.bind(window));
  } catch (err) {
    return {};
  }
}

async function boot() {
  const glCanvas = document.getElementById('glcanvas');
  const overlayCanvas = document.getElementById('overlay');
  const fatalErrorEl = document.getElementById('fatalError');

  const siteDefaults = await loadSiteDefaults();
  const settingsStore = createSettings(window.localStorage, siteDefaults);
  const model = createModel();

  let renderer;
  try {
    renderer = createRenderer(glCanvas, overlayCanvas);
  } catch (err) {
    showFatalError(fatalErrorEl, err);
    return;
  }

  applyRendererSettings(renderer, settingsStore.get());

  let currentHost = resolveHost(settingsStore.get());
  let currentPort = settingsStore.get().port;

  // `connection` and `ui` reference each other via callbacks; both are
  // declared up front and assigned before either is actually invoked
  // (connection callbacks only fire after connection.connect(), which
  // happens at the end of boot(), by which point `ui` is assigned).
  let connection;
  let ui;

  connection = createConnection({
    host: currentHost,
    port: currentPort,
    onHello() {
      // Informative only; protocol version is shown statically in the
      // About modal. Nothing else in the UI depends on hello contents.
    },
    onState(msg) {
      const nowMs = performance.now();
      model.applyState(msg, nowMs);
      renderer.setSnapshot(model.getSnapshot(), nowMs);
      ui.updateData(model);
    },
    onStatus(status) {
      ui.updateConnection(status, wsUrl(currentHost, currentPort));
    },
  });

  ui = createUi({
    settingsStore,
    callbacks: {
      onSelect(id) {
        renderer.setSelected(id);
      },
      onReconnect() {
        const s = settingsStore.get();
        currentHost = resolveHost(s);
        currentPort = s.port;
        connection.setEndpoint(currentHost, currentPort);
      },
    },
  });

  ui.updateConnection('connecting', wsUrl(currentHost, currentPort));

  settingsStore.onChange((next) => {
    applyRendererSettings(renderer, next);
    const resolvedHost = resolveHost(next);
    if (resolvedHost !== currentHost || next.port !== currentPort) {
      currentHost = resolvedHost;
      currentPort = next.port;
      connection.setEndpoint(currentHost, currentPort);
    }
  });

  wireOverlayPicking(overlayCanvas, () => renderer, () => ui);

  window.addEventListener('resize', () => renderer.resize());

  connection.connect();

  function raf(nowMs) {
    renderer.frame(nowMs);
    window.requestAnimationFrame(raf);
  }
  window.requestAnimationFrame(raf);

  setInterval(() => ui.tick(performance.now()), UI_TICK_INTERVAL_MS);
}

boot();
