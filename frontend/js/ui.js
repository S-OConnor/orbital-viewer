// ui.js — DOM wiring for the left panel, settings/about modals, and the
// object list. Together with main.js, this is one of the two DOM-touching
// modules (net.js/model.js/settings.js are DOM-free and Node-testable).
//
// createUi({settingsStore, callbacks:{onSelect(id), onReconnect()}})
//   → {updateConnection(status,url), updateData(model), setSelected(id|null),
//      tick(nowMs)}
//
// All DOM lookups are centralized here in one `nodes` map built at
// createUi() call time.

const CATEGORY_LIST = ['debris', 'star', 'comet', 'satellite', 'groundHot'];
const CATEGORY_LABEL = {
  debris: 'Debris',
  star: 'Star',
  comet: 'Comet',
  satellite: 'Satellite',
  groundHot: 'Ground hot',
};
const EARTH_RADIUS_KM = 6371.0;
const EM_DASH = '—';
const STALE_THRESHOLD_S = 3;

function capitalize(s) {
  return s.charAt(0).toUpperCase() + s.slice(1);
}

function fmtFixed(v, digits) {
  return v === null || v === undefined || !Number.isFinite(v) ? EM_DASH : v.toFixed(digits);
}

function fmtInt(v) {
  return v === null || v === undefined || !Number.isFinite(v) ? EM_DASH : String(v);
}

export function createUi({ settingsStore, callbacks = {} } = {}) {
  const doc = document;
  const byId = (id) => doc.getElementById(id);

  const nodes = {
    statusDot: byId('statusDot'),
    statusText: byId('statusText'),
    wsUrl: byId('wsUrl'),
    lastDataTime: byId('lastDataTime'),
    lastDataRelative: byId('lastDataRelative'),
    staleWarning: byId('staleWarning'),

    statUdpReceived: byId('statUdpReceived'),
    statUdpAccepted: byId('statUdpAccepted'),
    statUdpDropped: byId('statUdpDropped'),
    statUdpRateHz: byId('statUdpRateHz'),
    statWsClients: byId('statWsClients'),
    statObjectCount: byId('statObjectCount'),
    statBroadcastSeq: byId('statBroadcastSeq'),

    satId: byId('satId'),
    satSeq: byId('satSeq'),
    satX: byId('satX'),
    satY: byId('satY'),
    satZ: byId('satZ'),
    satSpeed: byId('satSpeed'),
    satAlt: byId('satAlt'),

    objectRows: byId('objectRows'),
    objectTableFooter: byId('objectTableFooter'),
    countAll: byId('countAll'),
    chipAll: doc.querySelector('.chip[data-filter=""]'),

    panel: byId('panel'),
    panelToggle: byId('panelToggle'),

    btnSettings: byId('btnSettings'),
    btnAbout: byId('btnAbout'),
    settingsModal: byId('settingsModal'),
    aboutModal: byId('aboutModal'),

    settingShowTrails: byId('settingShowTrails'),
    settingTrailSeconds: byId('settingTrailSeconds'),
    trailSecondsValue: byId('trailSecondsValue'),
    settingShowLabels: byId('settingShowLabels'),
    settingHost: byId('settingHost'),
    settingPort: byId('settingPort'),
    btnReconnect: byId('btnReconnect'),
    btnResetDefaults: byId('btnResetDefaults'),
  };

  for (const cat of CATEGORY_LIST) {
    nodes[`cat_${cat}`] = byId(`cat${capitalize(cat)}`);
    nodes[`chip_${cat}`] = doc.querySelector(`.chip[data-filter="${cat}"]`);
    nodes[`count_${cat}`] = byId(`count${capitalize(cat)}`);
  }

  let currentFilter = null;
  let selectedId = null;
  let lastDataTimeIso = null;
  let lastModel = null;

  // ---- Panel collapse (state not persisted) ----
  if (nodes.panelToggle && nodes.panel) {
    nodes.panelToggle.addEventListener('click', () => {
      const collapsed = nodes.panel.classList.toggle('collapsed');
      nodes.panelToggle.textContent = collapsed ? '❭' : '❬';
      nodes.panelToggle.setAttribute('aria-expanded', String(!collapsed));
    });
  }

  // ---- Category filter chips ----
  function setFilter(f) {
    currentFilter = f || null;
    const allChips = [nodes.chipAll, ...CATEGORY_LIST.map((c) => nodes[`chip_${c}`])];
    for (const chip of allChips) {
      if (!chip) continue;
      const chipFilter = chip.dataset.filter || '';
      chip.classList.toggle('active', chipFilter === (currentFilter || ''));
    }
  }
  if (nodes.chipAll) {
    nodes.chipAll.addEventListener('click', () => {
      setFilter(null);
      if (lastModel) renderList(lastModel);
    });
  }
  for (const cat of CATEGORY_LIST) {
    const chip = nodes[`chip_${cat}`];
    if (chip) {
      chip.addEventListener('click', () => {
        setFilter(cat);
        if (lastModel) renderList(lastModel);
      });
    }
  }

  // ---- Modals ----
  function openModal(modal) {
    if (modal) modal.classList.remove('hidden');
  }
  function closeModal(modal) {
    if (modal) modal.classList.add('hidden');
  }
  function closeAllModals() {
    closeModal(nodes.settingsModal);
    closeModal(nodes.aboutModal);
  }

  if (nodes.btnSettings) {
    nodes.btnSettings.addEventListener('click', () => {
      syncSettingsForm();
      openModal(nodes.settingsModal);
    });
  }
  if (nodes.btnAbout) {
    nodes.btnAbout.addEventListener('click', () => openModal(nodes.aboutModal));
  }
  doc.querySelectorAll('[data-close]').forEach((elm) => {
    elm.addEventListener('click', (e) => {
      const modal = e.target.closest('.modal');
      closeModal(modal);
    });
  });
  doc.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') closeAllModals();
  });

  // ---- Settings form <-> settings store (single source of truth) ----
  function syncSettingsForm() {
    const s = settingsStore.get();
    if (nodes.settingShowTrails) nodes.settingShowTrails.checked = s.showTrails;
    if (nodes.settingTrailSeconds) nodes.settingTrailSeconds.value = String(s.trailSeconds);
    if (nodes.trailSecondsValue) nodes.trailSecondsValue.textContent = String(s.trailSeconds);
    if (nodes.settingShowLabels) nodes.settingShowLabels.checked = s.showLabels;
    for (const cat of CATEGORY_LIST) {
      const cb = nodes[`cat_${cat}`];
      if (cb) cb.checked = !!s.categories[cat];
    }
    if (nodes.settingHost) nodes.settingHost.value = s.host;
    if (nodes.settingPort) nodes.settingPort.value = String(s.port);
  }

  if (nodes.settingShowTrails) {
    nodes.settingShowTrails.addEventListener('change', (e) => {
      settingsStore.update({ showTrails: e.target.checked });
    });
  }
  if (nodes.settingTrailSeconds) {
    nodes.settingTrailSeconds.addEventListener('input', (e) => {
      if (nodes.trailSecondsValue) nodes.trailSecondsValue.textContent = e.target.value;
      settingsStore.update({ trailSeconds: Number(e.target.value) });
    });
  }
  if (nodes.settingShowLabels) {
    nodes.settingShowLabels.addEventListener('change', (e) => {
      settingsStore.update({ showLabels: e.target.checked });
    });
  }
  for (const cat of CATEGORY_LIST) {
    const cb = nodes[`cat_${cat}`];
    if (cb) {
      cb.addEventListener('change', (e) => {
        settingsStore.update({ categories: { [cat]: e.target.checked } });
      });
    }
  }
  if (nodes.settingHost) {
    nodes.settingHost.addEventListener('change', (e) => {
      settingsStore.update({ host: e.target.value });
    });
  }
  if (nodes.settingPort) {
    nodes.settingPort.addEventListener('change', (e) => {
      settingsStore.update({ port: Number(e.target.value) });
    });
  }
  if (nodes.btnReconnect) {
    nodes.btnReconnect.addEventListener('click', () => {
      if (typeof callbacks.onReconnect === 'function') callbacks.onReconnect();
    });
  }
  if (nodes.btnResetDefaults) {
    nodes.btnResetDefaults.addEventListener('click', () => {
      settingsStore.resetDefaults();
      syncSettingsForm();
    });
  }

  settingsStore.onChange(() => {
    if (nodes.settingsModal && !nodes.settingsModal.classList.contains('hidden')) {
      syncSettingsForm();
    }
  });

  // ---- Object table: row click -> select ----
  if (nodes.objectRows) {
    nodes.objectRows.addEventListener('click', (e) => {
      const tr = e.target.closest('tr[data-id]');
      if (!tr) return;
      const id = Number(tr.dataset.id);
      setSelected(id);
      if (typeof callbacks.onSelect === 'function') callbacks.onSelect(id);
    });
  }

  function setSelected(id) {
    selectedId = id === undefined ? null : id;
    if (nodes.objectRows) {
      Array.from(nodes.objectRows.children).forEach((tr) => {
        tr.classList.toggle('selected', Number(tr.dataset.id) === selectedId);
      });
    }
  }

  // ---- Connection status ----
  function updateConnection(status, url) {
    if (nodes.statusText) nodes.statusText.textContent = status;
    if (nodes.statusDot) {
      nodes.statusDot.classList.remove('status-open', 'status-connecting', 'status-closed');
      nodes.statusDot.classList.add(`status-${status}`);
    }
    if (nodes.wsUrl) nodes.wsUrl.textContent = url || EM_DASH;
  }

  // ---- Data refresh (stats, telemetry, list, data time) ----
  function updateData(model) {
    lastModel = model;

    const stats = model.getStats();
    if (stats) {
      if (nodes.statUdpReceived) nodes.statUdpReceived.textContent = fmtInt(stats.udpReceived);
      if (nodes.statUdpAccepted) nodes.statUdpAccepted.textContent = fmtInt(stats.udpAccepted);
      if (nodes.statUdpDropped) nodes.statUdpDropped.textContent = fmtInt(stats.udpDropped);
      if (nodes.statUdpRateHz) nodes.statUdpRateHz.textContent = fmtFixed(stats.udpRateHz, 2);
      if (nodes.statWsClients) nodes.statWsClients.textContent = fmtInt(stats.wsClients);
      if (nodes.statObjectCount) nodes.statObjectCount.textContent = fmtInt(stats.objectCount);
      if (nodes.statBroadcastSeq) nodes.statBroadcastSeq.textContent = fmtInt(stats.broadcastSeq);
    }

    const snap = model.getSnapshot();
    const sat = snap.satellite;
    const satNodes = [nodes.satId, nodes.satSeq, nodes.satX, nodes.satY, nodes.satZ, nodes.satSpeed, nodes.satAlt];
    if (sat) {
      if (nodes.satId) nodes.satId.textContent = fmtInt(sat.id);
      if (nodes.satSeq) nodes.satSeq.textContent = fmtInt(sat.seq);
      if (nodes.satX) nodes.satX.textContent = fmtFixed(sat.pos[0] / 1000, 1);
      if (nodes.satY) nodes.satY.textContent = fmtFixed(sat.pos[1] / 1000, 1);
      if (nodes.satZ) nodes.satZ.textContent = fmtFixed(sat.pos[2] / 1000, 1);
      const speedKmS = Math.sqrt(sat.vel[0] ** 2 + sat.vel[1] ** 2 + sat.vel[2] ** 2) / 1000;
      if (nodes.satSpeed) nodes.satSpeed.textContent = fmtFixed(speedKmS, 2);
      const rKm = Math.sqrt(sat.pos[0] ** 2 + sat.pos[1] ** 2 + sat.pos[2] ** 2) / 1000;
      if (nodes.satAlt) nodes.satAlt.textContent = fmtFixed(rKm - EARTH_RADIUS_KM, 1);
    } else {
      satNodes.forEach((n) => {
        if (n) n.textContent = EM_DASH;
      });
    }

    lastDataTimeIso = model.getLastDataTime();
    updateDataTimeText();

    const counts = model.getCounts();
    if (nodes.countAll) nodes.countAll.textContent = String(counts.total);
    for (const cat of CATEGORY_LIST) {
      const n = nodes[`count_${cat}`];
      if (n) n.textContent = String(counts[cat]);
    }

    renderList(model);
  }

  function renderList(model) {
    if (!nodes.objectRows) return;
    const { rows, total } = model.getListRows(currentFilter, 1000);
    const frag = doc.createDocumentFragment();
    for (const row of rows) {
      const tr = doc.createElement('tr');
      tr.dataset.id = String(row.id);
      if (row.id === selectedId) tr.classList.add('selected');
      const tdId = doc.createElement('td');
      tdId.textContent = String(row.id);
      const tdCat = doc.createElement('td');
      tdCat.textContent = CATEGORY_LABEL[row.cat] || row.cat;
      const tdConf = doc.createElement('td');
      tdConf.textContent = String(row.conf);
      tr.appendChild(tdId);
      tr.appendChild(tdCat);
      tr.appendChild(tdConf);
      frag.appendChild(tr);
    }
    nodes.objectRows.replaceChildren(frag);
    if (nodes.objectTableFooter) {
      nodes.objectTableFooter.textContent = `showing ${rows.length} of ${total}`;
    }
  }

  function updateDataTimeText() {
    if (!nodes.lastDataTime) return;
    if (!lastDataTimeIso) {
      nodes.lastDataTime.textContent = EM_DASH;
      if (nodes.lastDataRelative) nodes.lastDataRelative.textContent = '';
      return;
    }
    nodes.lastDataTime.textContent = lastDataTimeIso;
    const dataMs = Date.parse(lastDataTimeIso);
    if (Number.isFinite(dataMs) && nodes.lastDataRelative) {
      const seconds = Math.max(0, (Date.now() - dataMs) / 1000);
      nodes.lastDataRelative.textContent = `(${seconds.toFixed(1)} s ago)`;
    }
  }

  // ---- 1 Hz refresh of relative time + staleness ----
  function tick(nowMs) {
    updateDataTimeText();
    if (nodes.staleWarning) {
      const stale = lastModel ? lastModel.secondsSinceLastState(nowMs) > STALE_THRESHOLD_S : false;
      nodes.staleWarning.classList.toggle('hidden', !stale);
    }
  }

  syncSettingsForm();
  setFilter(null);

  return { updateConnection, updateData, setSelected, tick };
}
