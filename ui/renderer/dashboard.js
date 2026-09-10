// Edgeline: the dashboard that lives on the panel.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A 2560x720 strip is an awkward shape for anything but a row of tiles, which
// is what the design settled on. Everything here comes from the agent's sensor
// stream; this window reads no files and runs no commands of its own.
'use strict';

const api = window.edgeline;
const strip = document.getElementById('strip');
const offline = document.getElementById('offline');

let sensors = {};
let layout = { page: 0, tiles: {} };

const fmt1 = (v) => (v >= 0 ? v.toFixed(1) : '—');
const pct = (v) => (v >= 0 ? `${Math.round(v)}%` : '—');

// Every tile the design lists. Two of them describe things this build does not
// collect, and they say so on the panel rather than showing a plausible number.
const TILES = {
  Clock: () => {
    const now = new Date();
    const hh = String(now.getHours()).padStart(2, '0');
    const mm = String(now.getMinutes()).padStart(2, '0');
    return { big: `${hh}:${mm}`, sub: now.toLocaleDateString(undefined,
      { weekday: 'short', day: 'numeric', month: 'short' }), size: 40, span: 2 };
  },
  CPU: () => ({
    kicker: 'CPU',
    tag: sensors.cpuTempC >= 0 ? `${Math.round(sensors.cpuTempC)}°` : '',
    big: pct(sensors.cpuLoadPct),
    sub: `${navigator.hardwareConcurrency || '?'} threads`,
    pctValue: sensors.cpuLoadPct, size: 26, span: 1,
  }),
  GPU: () => (sensors.gpuOk
    ? { kicker: 'GPU', tag: sensors.gpuTempC >= 0 ? `${Math.round(sensors.gpuTempC)}°` : '',
        big: pct(sensors.gpuUtilPct), sub: sensors.gpuName || '',
        pctValue: sensors.gpuUtilPct, size: 26, span: 1 }
    : { kicker: 'GPU', big: '—', sub: 'nvidia-smi unavailable', size: 26, span: 1, muted: true }),
  Memory: () => ({
    kicker: 'MEM',
    big: sensors.ramTotalGiB ? `${fmt1(sensors.ramUsedGiB)} GB` : '—',
    sub: sensors.ramTotalGiB ? `of ${fmt1(sensors.ramTotalGiB)} GB` : '',
    pctValue: sensors.ramPct, size: 24, span: 1,
  }),
  Network: () => ({
    kicker: 'NET', tag: sensors.netInterface || '',
    big: sensors.netRxMBs >= 0 ? `↓ ${fmt1(sensors.netRxMBs)}` : '—',
    sub: sensors.netTxMBs >= 0 ? `↑ ${fmt1(sensors.netTxMBs)} MB/s` : '',
    size: 24, span: 1,
  }),
  'Disk I/O': () => ({
    kicker: 'DISK', tag: sensors.diskDevice || '',
    big: sensors.diskReadMBs >= 0
      ? `${fmt1(sensors.diskReadMBs + sensors.diskWriteMBs)} MB/s` : '—',
    sub: sensors.diskReadMBs >= 0
      ? `r ${fmt1(sensors.diskReadMBs)} · w ${fmt1(sensors.diskWriteMBs)}` : '',
    size: 22, span: 1,
  }),
  'systemd units': () => ({
    kicker: 'UNITS',
    big: sensors.failedUnits >= 0
      ? (sensors.failedUnits === 0 ? 'all ok' : `${sensors.failedUnits} failed`) : '—',
    sub: (sensors.failedUnitNames || [])
      .map((u) => u.replace(/\.service$/, '')).slice(0, 3).join(' · '),
    size: 22, span: 1,
  }),
  'Now playing': () => ({
    kicker: 'NOW PLAYING', big: 'not available',
    sub: 'needs an MPRIS reader', size: 22, span: 2, muted: true,
  }),
  Notifications: () => ({
    kicker: 'NOTIFY', big: 'not available',
    sub: 'needs a D-Bus notification monitor', size: 22, span: 2, muted: true,
  }),
};

const PAGES = [
  { label: 'System', keys: ['Clock', 'CPU', 'GPU', 'Memory', 'Disk I/O', 'systemd units'] },
  { label: 'Media', keys: ['Clock', 'Now playing', 'Notifications', 'Network'] },
];

function el(tag, cls, text) {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
}

// The design's tile sizes come from its 260px-tall preview. The panel is 720px,
// so everything is scaled to it: at preview sizes the strip reads as a row of
// mostly empty boxes from across a desk.
function scale() {
  return Math.max(1, window.innerHeight / 260);
}

function render() {
  const k = scale();
  const page = PAGES[layout.page] || PAGES[0];
  const keys = page.keys.filter((k) => layout.tiles[k] !== false && TILES[k]);
  const specs = keys.map((k) => ({ key: k, ...TILES[k]() }));
  const columns = specs.reduce((n, s) => n + (s.span || 1), 0) || 1;
  strip.style.gridTemplateColumns = `repeat(${columns}, minmax(0, 1fr))`;

  strip.replaceChildren(...specs.map((s) => {
    const t = el('div', `tile${s.muted ? ' muted' : ''}`);
    t.style.gridColumn = `span ${s.span || 1}`;
    t.style.setProperty('--k', String(k));
    if (s.kicker || s.tag) {
      const head = el('div', 'head');
      head.append(el('div', 'kicker', s.kicker || ''));
      if (s.tag) head.append(el('div', 'tag', s.tag));
      t.append(head);
    }
    const big = el('div', 'big', s.big);
    big.style.fontSize = `${Math.round(s.size * k)}px`;
    t.append(big);
    if (s.sub) t.append(el('div', 'sub', s.sub));
    if (typeof s.pctValue === 'number' && s.pctValue >= 0) {
      const track = el('div', 'track');
      const fill = el('div', 'fill');
      fill.style.width = `${Math.max(0, Math.min(100, s.pctValue))}%`;
      track.append(fill);
      t.append(track);
    }
    return t;
  }));
}

api.onEvent((msg) => {
  if (msg.event === 'sensors') { sensors = msg.data; render(); }
  else if (msg.event === 'dashboard') { layout = { ...layout, ...msg.data }; render(); }
});

api.onStatus((st) => {
  offline.classList.toggle('show', !st.connected);
  if (st.connected) start();
});

async function start() {
  try {
    const all = await api.call('state.all');
    sensors = all.sensors || {};
    // The panel is the whole point of the sensor stream, so it is started
    // here rather than left to whichever window happens to be open.
    await api.call('sensors.stream', { enabled: true, intervalMs: 1000 });
  } catch { /* the status handler will retry on reconnect */ }
  render();
}

// The clock has to tick even when nothing else changes.
setInterval(render, 1000);
start();
