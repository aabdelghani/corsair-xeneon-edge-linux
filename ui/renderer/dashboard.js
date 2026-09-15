// EdgeLine: the dashboard that lives on the panel.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Authored at exactly 2560x720, the panel's own size, and scaled to whatever
// the window turns out to be, so what lands on the glass is the artboard
// rather than an approximation of it.
//
// Everything here comes from the agent's sensor stream. This window reads no
// files and runs no commands of its own, and where the agent has no source for
// a figure the tile says so rather than showing a plausible number.
'use strict';

const api = window.edgeline;
const board = document.getElementById('board');
const offline = document.getElementById('offline');

let sensors = {};
let layout = { page: 0, tiles: {} };
let lastSensorMs = 0;

// One sample a second, 48 of them, which is the width the design's sparklines
// were drawn for. History lives here rather than in the agent so that it
// survives an agent restart no worse than the window does.
const HISTORY = 48;
const cpuHistory = [];
const gpuHistory = new Map();   // gpu id -> samples

const PAGE_NAMES = ['SYSTEM', 'MEDIA', 'TILES'];

// [tile, columns, rows]. Every page fills the twelve by three grid exactly, in
// DOM order, so the browser's own auto-placement reproduces the artboard with
// no explicit row or column numbers to drift.
const LAYOUTS = [
  [['time', 3, 2], ['cpu', 3, 1], ['gpu', 3, 1], ['notifications', 3, 3],
   ['memory', 2, 1], ['disk', 2, 1], ['network', 2, 1],
   ['nowplaying', 3, 1], ['units', 2, 1], ['cooling', 2, 1], ['power', 2, 1]],

  [['time', 3, 2], ['nowplaying', 6, 2], ['notifications', 3, 3],
   ['memory', 3, 1], ['disk', 3, 1], ['network', 3, 1]],

  [['cpu', 3, 1], ['gpu', 3, 1], ['memory', 3, 1], ['disk', 3, 1],
   ['network', 3, 1], ['units', 3, 1], ['cooling', 3, 1], ['power', 3, 1],
   ['time', 6, 1], ['nowplaying', 6, 1]],
];

// The control window hides tiles by its own labels; these are the same tiles.
const LABEL_OF = {
  time: 'Clock', cpu: 'CPU', gpu: 'GPU', memory: 'Memory', disk: 'Disk I/O',
  network: 'Network', units: 'systemd units', nowplaying: 'Now playing',
  notifications: 'Notifications', cooling: 'Cooling', power: 'Power',
};

// ------------------------------------------------------------------ helpers

const pad = (n) => String(n).padStart(2, '0');
const one = (v) => (v >= 0 ? v.toFixed(1) : '—');
const whole = (v) => (v >= 0 ? String(Math.round(v)) : '—');

function h(tag, cls, text) {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined && text !== null) n.textContent = text;
  return n;
}

function add(parent, ...kids) {
  for (const k of kids) if (k) parent.append(k);
  return parent;
}

function card(kind, cols, rows) {
  return h('div', `card ${kind} c${cols}${rows > 1 ? ` r${rows}` : ''}`);
}

function uptimeText(sec) {
  const d = Math.floor(sec / 86400);
  const hh = Math.floor((sec % 86400) / 3600);
  const mm = Math.floor((sec % 3600) / 60);
  return d > 0 ? `${d}d ${pad(hh)}:${pad(mm)}` : `${pad(hh)}:${pad(mm)}`;
}

// A snapshot older than a few seconds means the stream has stopped. Say so by
// stopping the pulse rather than leaving stale numbers looking live.
const fresh = () => lastSensorMs > 0 && Date.now() - lastSensorMs < 5000;

function agoText(ms) {
  if (!ms) return '';
  const s = Math.max(0, Math.round((Date.now() - ms) / 1000));
  if (s < 60) return `${s}s`;
  if (s < 3600) return `${Math.round(s / 60)}m`;
  return `${Math.round(s / 3600)}h`;
}

function gpuList() {
  if (Array.isArray(sensors.gpus)) return sensors.gpus;
  // An agent older than this window sends only the flat fields.
  return sensors.gpuOk
    ? [{ id: '', name: sensors.gpuName, source: sensors.gpuSource, utilPct: sensors.gpuUtilPct,
         tempC: sensors.gpuTempC, powerW: sensors.gpuPowerW }]
    : [];
}

function shownGpus() {
  const all = gpuList();
  if (!Array.isArray(sensors.gpuIdsShown)) return all.slice(0, 1);
  const picked = sensors.gpuIdsShown.map((id) => all.find((g) => g.id === id)).filter(Boolean);
  return picked.length ? picked : all.slice(0, 1);
}

// Two GPU names in one tile leave little room, and "NVIDIA GeForce RTX 5090"
// is mostly prefix.
const shortGpuName = (n) => (n || '')
  .replace(/^NVIDIA\s+(GeForce\s+)?/i, '')
  .replace(/^AMD\s+/i, '');

function sample() {
  if (sensors.cpuLoadPct >= 0) push(cpuHistory, sensors.cpuLoadPct);
  for (const g of shownGpus()) {
    if (!gpuHistory.has(g.id)) gpuHistory.set(g.id, []);
    if (g.utilPct >= 0) push(gpuHistory.get(g.id), g.utilPct);
  }
}

function push(arr, v) {
  arr.push(v);
  while (arr.length > HISTORY) arr.shift();
}

const SVG = 'http://www.w3.org/2000/svg';

function spark(hist, cls) {
  const svg = document.createElementNS(SVG, 'svg');
  svg.setAttribute('viewBox', '0 0 100 30');
  svg.setAttribute('preserveAspectRatio', 'none');
  svg.setAttribute('class', `spark ${cls}`);
  if (hist.length >= 2) {
    // Floor the scale at 60% so an idle machine draws a flat line near the
    // bottom instead of amplifying noise into a mountain range.
    const max = Math.max(60, ...hist);
    const line = document.createElementNS(SVG, 'polyline');
    line.setAttribute('points', hist.map((v, i) =>
      `${(i / (hist.length - 1) * 100).toFixed(2)},${(30 - (v / max) * 28).toFixed(2)}`).join(' '));
    line.setAttribute('vector-effect', 'non-scaling-stroke');
    svg.append(line);
  }
  return svg;
}

// A tile with nothing behind it. Honest, and at the size of the number it
// would otherwise be showing.
function unavailable(c, what, why) {
  add(c, h('div', 'none', what));
  if (why) add(c, h('div', 'why', why));
  return c;
}

// -------------------------------------------------------------------- tiles

const TILES = {
  time: (cols, rows) => {
    const c = card('time', cols, rows);
    const now = new Date();
    add(c, h('div', 'kicker', 'LOCAL TIME'));
    add(c, add(h('div', 'row'),
      h('div', 'clock', `${pad(now.getHours())}:${pad(now.getMinutes())}`),
      h('div', 'secs', pad(now.getSeconds()))));
    const foot = h('div', 'foot');
    add(foot, h('span', null, now.toLocaleDateString(undefined,
      { weekday: 'long', month: 'short', day: 'numeric' }).toUpperCase()));
    if (sensors.uptimeSec >= 0) add(foot, h('span', 'up', `UP ${uptimeText(sensors.uptimeSec)}`));
    return add(c, foot);
  },

  cpu: (cols, rows) => {
    const c = card('load', cols, rows);
    const head = add(h('div', 'head'), h('span', 'kicker', 'CPU'));
    const bits = [];
    if (sensors.cpuTempC >= 0) bits.push(`${Math.round(sensors.cpuTempC)}°C`);
    if (sensors.cpuCores > 0 && sensors.cpuThreads > 0)
      bits.push(`${sensors.cpuCores}C/${sensors.cpuThreads}T`);
    if (bits.length) add(head, h('span', 'meta', bits.join(' · ')));
    add(c, head);
    add(c, add(h('div', 'value'),
      h('span', 'num', whole(sensors.cpuLoadPct)), h('span', 'unit', '%')));
    return add(c, add(h('div', 'chart'), spark(cpuHistory, 'cpu')));
  },

  gpu: (cols, rows) => {
    const c = card('load', cols, rows);
    const shown = shownGpus();
    const head = add(h('div', 'head'), h('span', 'kicker', 'GPU'));

    if (!shown.length) {
      add(c, head);
      return unavailable(c, 'no GPU telemetry',
        'nvidia-smi is not answering and no amdgpu card reports through sysfs');
    }
    if (shown.length === 1) {
      const bits = [];
      if (shown[0].tempC >= 0) bits.push(`${Math.round(shown[0].tempC)}°C`);
      if (shown[0].source) bits.push(shown[0].source);
      if (bits.length) add(head, h('span', 'meta', bits.join(' · ')));
    } else {
      add(head, h('span', 'meta', `${shown.length} cards`));
    }
    add(c, head);

    // Two cards share this tile rather than taking a column from something
    // else: the grid is exactly full at twelve by three.
    const wrap = h('div', `gpus${shown.length > 1 ? ' two' : ''}`);
    for (const g of shown) {
      const row = h('div', 'gpu-row');
      add(row, add(h('div', 'value'),
        h('span', 'num', whole(g.utilPct)), h('span', 'unit', '%')));
      if (shown.length > 1) add(row, h('div', 'name', shortGpuName(g.name)));
      add(row, add(h('div', 'chart'), spark(gpuHistory.get(g.id) || [], 'gpu')));
      add(wrap, row);
    }
    return add(c, wrap);
  },

  memory: (cols, rows) => {
    const c = card('stat', cols, rows);
    add(c, h('div', 'kicker', 'MEMORY'));
    const big = h('div', 'big', sensors.ramTotalGiB ? one(sensors.ramUsedGiB) : '—');
    if (sensors.ramTotalGiB)
      add(big, h('span', 'unit', ` / ${Math.round(sensors.ramTotalGiB)} GB`));
    add(c, big);
    const fill = h('div', 'fill');
    fill.style.width = `${Math.max(0, Math.min(100, sensors.ramPct >= 0 ? sensors.ramPct : 0))}%`;
    return add(c, add(h('div', 'bar'), fill));
  },

  disk: (cols, rows) => {
    const c = card('stat', cols, rows);
    add(c, h('div', 'kicker', 'DISK I/O'));
    const total = sensors.diskReadMBs >= 0 ? sensors.diskReadMBs + sensors.diskWriteMBs : -1;
    const big = h('div', 'big', one(total));
    add(big, h('span', 'unit', ' MB/s'));
    add(c, big);
    const bits = [];
    if (sensors.diskDevice) bits.push(sensors.diskDevice);
    if (sensors.diskUsedPct >= 0) bits.push(`${Math.round(sensors.diskUsedPct)}% full`);
    return add(c, h('div', 'sub', bits.join(' · ')));
  },

  network: (cols, rows) => {
    const c = card('stat', cols, rows);
    add(c, h('div', 'kicker', 'NETWORK'));
    // The counters are bytes; the tile is drawn in bits, as network gear is
    // always quoted.
    const mbit = (mb) => (mb >= 0 ? Math.round(mb * 8) : -1);
    const down = mbit(sensors.netRxMBs);
    const up = mbit(sensors.netTxMBs);
    const big = h('div', 'big', down >= 0 ? String(down + up) : '—');
    add(big, h('span', 'unit', ' Mb/s'));
    add(c, big);
    return add(c, h('div', 'sub', down >= 0
      ? `↓ ${down} Mb · ↑ ${up} Mb${sensors.netInterface ? ` · ${sensors.netInterface}` : ''}`
      : ''));
  },

  units: (cols, rows) => {
    const c = card('stat', cols, rows);
    add(c, h('div', 'kicker', 'SYSTEMD'));
    const n = sensors.failedUnits;
    const count = h('div', 'count');
    add(count, h('span', `n${n > 0 ? ' bad' : ''}`, n >= 0 ? String(n) : '—'),
               h('span', 'word', n === 0 ? 'all units ok' : 'failed'));
    add(c, count);
    const names = (sensors.failedUnitNames || []).slice(0, 2)
      .map((u) => u.replace(/\.service$/, ''));
    return add(c, h('div', 'names', names.join('\n')));
  },

  cooling: (cols, rows) => {
    const c = card('stat', cols, rows);
    add(c, h('div', 'kicker', 'COOLING'));
    if (!(sensors.fanRpm >= 0))
      return unavailable(c, 'no fan sensor',
        'nothing under /sys/class/hwmon reports a fan on this machine');
    const big = h('div', 'big', String(Math.round(sensors.fanRpm)));
    add(big, h('span', 'unit', ' RPM'));
    add(c, big);
    return add(c, h('div', 'sub', sensors.fanChip || ''));
  },

  power: (cols, rows) => {
    const c = card('stat', cols, rows);
    add(c, h('div', 'kicker', 'POWER'));
    if (sensors.packageWatts >= 0) {
      const big = h('div', 'big', String(Math.round(sensors.packageWatts)));
      add(big, h('span', 'unit', ' W'));
      add(c, big);
      return add(c, h('div', 'sub', 'package, from powercap'));
    }
    // No package counter, but a GPU that reports its own draw is still a real
    // measurement. It is labelled for what it is rather than passed off as the
    // whole machine.
    const g = shownGpus().find((x) => x.powerW >= 1);
    if (g) {
      const big = h('div', 'big', String(Math.round(g.powerW)));
      add(big, h('span', 'unit', ' W'));
      add(c, big);
      return add(c, h('div', 'sub', `${shortGpuName(g.name)} only`));
    }
    return unavailable(c, 'no power sensor',
      "powercap's energy counters are readable by root only here");
  },

  nowplaying: (cols, rows) => {
    const np = sensors.nowPlaying || {};
    if (!np.valid) {
      const c = card('stat', cols, rows);
      add(c, h('div', 'kicker', 'NOW PLAYING'));
      return sensors.sessionBusOk === false
        ? unavailable(c, 'no session bus', 'the agent cannot reach a D-Bus session bus')
        : unavailable(c, 'nothing playing', 'no MPRIS player has a track loaded');
    }
    const c = card('media', cols, rows);
    // The cover art stays a placeholder on purpose: players publish it as a
    // file:// or https:// URL, and letting the panel fetch either would mean
    // opening the content security policy for the sake of a thumbnail.
    add(c, h('div', 'art', (np.player || 'MPRIS').toUpperCase()));
    const body = h('div', 'body');
    add(body, h('div', 'kicker',
      np.status && np.status !== 'Playing' ? np.status.toUpperCase() : 'NOW PLAYING'));
    add(body, h('div', 'title', np.title));
    add(body, h('div', 'by', [np.artist, np.album].filter(Boolean).join(' — ')));
    if (np.lengthUs > 0 && np.positionUs >= 0) {
      const fill = h('div', 'fill');
      fill.style.width = `${Math.max(0, Math.min(100, (np.positionUs / np.lengthUs) * 100))}%`;
      add(body, add(h('div', 'bar'), fill));
    }
    return add(c, body);
  },

  notifications: (cols, rows) => {
    const c = card('notify', cols, rows);
    const list = sensors.notifications || [];
    const head = add(h('div', 'head'), h('span', 'kicker', 'NOTIFICATIONS'));
    if (list.length) add(head, h('span', 'badge', `${list.length} NEW`));
    add(c, head);

    if (sensors.notifyActive === false)
      return unavailable(c, 'not watching',
        sensors.notifyError || 'the session bus would not make the agent a monitor');
    if (!list.length)
      return unavailable(c, 'nothing new', 'notifications appear here as they arrive');

    const wrap = h('div', 'list');
    for (const n of list) {
      const top = add(h('div', 'top'),
        h('span', 'app', (n.app || '').toUpperCase()),
        h('span', 'when', agoText(n.whenMs)));
      add(wrap, add(h('div', 'item'), add(h('div', 'body'),
        top, h('div', 'title', n.summary), h('div', 'text', n.body))));
    }
    add(c, wrap);

    const clear = h('div', null, 'CLEAR ALL');
    clear.addEventListener('click', () => { api.call('notifications.clear').catch(() => {}); });
    // Do not disturb would mean telling the notification daemon to be quiet.
    // A monitor connection is forbidden from sending anything at all, which is
    // the point of it, so this is shown dimmed rather than as a button that
    // would do nothing.
    return add(c, add(h('div', 'actions'), clear, h('div', 'off', 'DO NOT DISTURB')));
  },
};

// ------------------------------------------------------------------- render

function fit() {
  // Letterbox rather than stretch: the artboard's proportions are the panel's.
  const s = Math.min(window.innerWidth / 2560, window.innerHeight / 720);
  board.style.setProperty('--s', String(s > 0 ? s : 1));
}

function header() {
  const hdr = h('div', 'hdr');
  add(hdr, add(h('div', 'brand'),
    h('div', `dot${fresh() ? '' : ' stale'}`),
    h('div', 'wordmark', 'Edgeline')));
  add(hdr, h('div', 'where', 'XENEON EDGE · 2560×720'));
  add(hdr, h('div', 'gap'));
  add(hdr, add(h('div', 'host'),
    h('span', 'label', 'HOST'),
    h('span', 'value', sensors.hostName || '—')));

  const pills = h('div', 'pills');
  PAGE_NAMES.forEach((name, i) => {
    const p = h('div', `pill${layout.page === i ? ' on' : ''}`, name);
    p.addEventListener('click', () => { layout.page = i; render(); });
    add(pills, p);
  });
  return add(hdr, pills);
}

function render() {
  const page = LAYOUTS[layout.page] || LAYOUTS[0];
  const grid = h('div', 'grid');
  for (const [key, cols, rows] of page) {
    if (layout.tiles[LABEL_OF[key]] === false) continue;
    add(grid, TILES[key](cols, rows));
  }
  board.replaceChildren(header(), grid);
}

api.onEvent((msg) => {
  if (msg.event === 'sensors') {
    sensors = msg.data;
    lastSensorMs = Date.now();
    sample();
    render();
  } else if (msg.event === 'dashboard') {
    layout = { ...layout, ...msg.data };
    render();
  }
});

api.onStatus((st) => {
  offline.classList.toggle('show', !st.connected);
  if (st.connected) start();
});

async function start() {
  try {
    const all = await api.call('state.all');
    sensors = all.sensors || {};
    lastSensorMs = Date.now();
    sample();
    // The panel is the whole point of the sensor stream, so it is started here
    // rather than left to whichever window happens to be open.
    await api.call('sensors.stream', { enabled: true, intervalMs: 1000 });
  } catch { /* the status handler will retry on reconnect */ }
  render();
}

window.addEventListener('resize', () => { fit(); render(); });

// The clock has to tick even when nothing else changes.
setInterval(render, 1000);
fit();
start();
