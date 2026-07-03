// model.js — in-memory client-side view of the latest server state.
//
// Pure logic module: no window/document/WebSocket access. Consumes parsed
// messages produced by net.js#parseStateMessage and exposes the shapes the
// renderer (frozen API, see docs/PLAN.md §6) and ui.js need.

function emptyCounts() {
  return { debris: 0, star: 0, comet: 0, satellite: 0, groundHot: 0, total: 0 };
}

/**
 * createModel() → {applyState, getSnapshot, getListRows, getCounts,
 *                   getLastDataTime, secondsSinceLastState, getStats}
 *
 * getStats() is an additive extension beyond the PLAN.md §6 summary
 * signature (consistent with the brief: "the panels need it").
 */
export function createModel() {
  let satellite = null;
  let objects = [];
  let lastDataTime = null;
  let serverTime = null;
  let stats = null;
  let lastAppliedAtMs = null;

  function applyState(msg, nowMs) {
    if (!msg || msg.type !== 'state') return;
    satellite = msg.satellite === undefined ? null : msg.satellite;
    objects = Array.isArray(msg.objects) ? msg.objects : [];
    lastDataTime = msg.lastDataTime === undefined ? null : msg.lastDataTime;
    serverTime = msg.serverTime === undefined ? null : msg.serverTime;
    stats = msg.stats === undefined ? null : msg.stats;
    lastAppliedAtMs = nowMs;
  }

  function getSnapshot() {
    return { satellite, objects, lastDataTime, serverTime };
  }

  function getListRows(filter, cap = 1000) {
    const filtered = filter ? objects.filter((o) => o.cat === filter) : objects.slice();
    filtered.sort((a, b) => a.id - b.id);
    const total = filtered.length;
    return { rows: filtered.slice(0, cap), total };
  }

  function getCounts() {
    const counts = emptyCounts();
    for (const obj of objects) {
      if (Object.prototype.hasOwnProperty.call(counts, obj.cat)) {
        counts[obj.cat] += 1;
      }
      counts.total += 1;
    }
    return counts;
  }

  function getLastDataTime() {
    return lastDataTime;
  }

  function secondsSinceLastState(nowMs) {
    if (lastAppliedAtMs === null) return Infinity;
    return (nowMs - lastAppliedAtMs) / 1000;
  }

  function getStats() {
    return stats;
  }

  return {
    applyState,
    getSnapshot,
    getListRows,
    getCounts,
    getLastDataTime,
    secondsSinceLastState,
    getStats,
  };
}
