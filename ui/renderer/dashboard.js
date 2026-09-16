// EdgeLine: the dashboard that lives on the panel.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Authored at exactly 2560x720, the panel's own size, and scaled to fill the
// window, so what lands on the glass is the artboard rather than an
// approximation of it.
//
// Everything here comes from the agent's sensor stream. This window reads no
// files and runs no commands of its own, and where the agent has no source for
// a figure the tile says so rather than showing a plausible number.
'use strict';

const api = window.edgeline;
const board = document.getElementById('board');
const offline = document.getElementById('offline');

let sensors = {};
let layout = { tiles: {} };

// ------------------------------------------------------------------- theme

const THEMES = ['night', 'daylight', 'porcelain', 'sage', 'user'];

// The User theme has no block in dashboard.css. Its three chosen colours are
// expanded into the full token set here and set on the root element, so every
// tile follows them. Anything not derived (the accent, its text) keeps the
// stylesheet's defaults.
const USER_DEFAULT = { bg: '#07080A', card: '#101216', text: '#E8EAED' };
let userTheme = { ...USER_DEFAULT };
let currentTheme = 'night';

const validHex = (h) => typeof h === 'string' && /^#[0-9a-fA-F]{6}$/.test(h);

function mix(a, b, t) {
  const pa = [1, 3, 5].map((i) => parseInt(a.slice(i, i + 2), 16));
  const pb = [1, 3, 5].map((i) => parseInt(b.slice(i, i + 2), 16));
  return `#${pa.map((v, i) => Math.round(v + (pb[i] - v) * t).toString(16).padStart(2, '0')).join('')}`;
}

function userTokens(u) {
  const { bg, card, text } = u;
  return {
    '--bg': bg, '--card': card, '--text': text,
    '--line': mix(card, text, 0.22), '--kicker': mix(text, card, 0.32), '--text2': mix(text, card, 0.18),
    '--item': mix(card, bg, 0.45), '--item-line': mix(card, text, 0.14), '--item-edge': mix(card, text, 0.35),
    '--track': mix(card, text, 0.14), '--media-fill': mix(text, card, 0.18),
    '--art': mix(card, bg, 0.45), '--art-line': mix(card, text, 0.14),
    '--btn-line': mix(card, text, 0.22), '--btn-bg': 'transparent',
    '--app': text, '--spark2': mix(text, card, 0.3),
  };
}

function setUserTheme(u) {
  if (!u || typeof u !== 'object') return;
  userTheme = {
    bg: validHex(u.bg) ? u.bg : USER_DEFAULT.bg,
    card: validHex(u.card) ? u.card : USER_DEFAULT.card,
    text: validHex(u.text) ? u.text : USER_DEFAULT.text,
  };
}
// Theme, page and tile visibility are saved by the main process, in a file
// written the moment they change, and handed to this window through appInfo.
// That matters at login, when the panel opens with no control window and still
// has to come up the way it was left. They used to live in localStorage, which
// Chromium flushes lazily and which lost a choice made just before a restart.
function applyTheme(name) {
  currentTheme = THEMES.includes(name) ? name : 'night';
  const root = document.documentElement;
  root.dataset.theme = currentTheme;
  // Clear what a previous User theme set, so switching away leaves nothing
  // behind that would override the stylesheet's own theme blocks.
  for (const k of Object.keys(userTokens(USER_DEFAULT))) root.style.removeProperty(k);
  if (currentTheme === 'user')
    for (const [k, v] of Object.entries(userTokens(userTheme))) root.style.setProperty(k, v);
}

function setPage(name) {
  layout.page = name === 'media' ? 'media' : 'system';
}

applyTheme(null);
setPage(null);

// Saved choices first, then --edgeline-panel-theme and --edgeline-panel-page,
// which win for this window only and are never saved, so a screenshot run
// cannot change anyone's choice.
api.appInfo().then((info) => {
  const prefs = (info && info.panelPrefs) || {};
  setUserTheme(prefs.userTheme);
  applyTheme(prefs.theme);
  setPage(prefs.page);
  if (prefs.tiles && typeof prefs.tiles === 'object') layout.tiles = prefs.tiles;
  if (info && info.panelTheme) applyTheme(info.panelTheme);
  if (info && info.panelPage) setPage(info.panelPage);
  render();
}).catch(() => {});

// ----------------------------------------------------------------- history

// One sample a second. CPU and GPU keep 48, the width the design's sparklines
// were drawn for; power keeps 64 for its wider tile. History lives here rather
// than in the agent, so it survives an agent restart no worse than the window.
const HISTORY = 48;
const POWER_HISTORY = 64;
const cpuHistory = [];
const gpuHistory = new Map();   // gpu id -> samples
const powerHistory = [];

// [tile, columns, rows]. Each page fills the twelve by three grid exactly, in
// DOM order, so the browser's own auto-placement reproduces the artboard. The
// panel itself carries no page switcher; the page is chosen on the Dashboard
// page of the control window.
const LAYOUTS = {
  system: [
    ['time', 3, 2], ['cpu', 3, 1], ['gpu', 3, 1], ['notifications', 3, 3],
    ['memory', 2, 1], ['disk', 2, 1], ['network', 2, 1],
    ['nowplaying', 5, 1], ['power', 4, 1],
  ],
  media: [
    ['time', 3, 2], ['nowplaying', 6, 2], ['notifications', 3, 3],
    ['memory', 3, 1], ['network', 3, 1], ['power', 3, 1],
  ],
};

// The control window hides tiles by its own labels; these are the same tiles.
const LABEL_OF = {
  time: 'Clock', cpu: 'CPU', gpu: 'GPU', memory: 'Memory', disk: 'Disk I/O',
  network: 'Network', nowplaying: 'Now playing',
  notifications: 'Notifications', power: 'Power',
};

// ----------------------------------------------------------------- helpers

const pad = (n) => String(n).padStart(2, '0');
const one = (v) => (v >= 0 ? v.toFixed(1) : '—');
const whole = (v) => (v >= 0 ? String(Math.round(v)) : '—');

function h(tag, cls, text) {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined && text !== null) n.textContent = text;
  return n;
}

// A number in a right-aligned box a fixed number of characters wide. Every
// panel number is set in a monospaced face, so the box holds any value up to
// that length and whatever follows it, the unit or the rest of the line, stays
// where it is as the value goes from 7 to 44 to 310.
function fixed(text, chars, cls) {
  const n = h('span', cls ? `${cls} fixed` : 'fixed', text);
  n.style.minWidth = `${chars}ch`;
  return n;
}

// Mixed text and fixed-width numbers on one line: strings pass through,
// [text, chars] pairs become boxes.
function line(cls, ...parts) {
  const n = h('div', cls);
  for (const p of parts) n.append(Array.isArray(p) ? fixed(p[0], p[1]) : p);
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

// "NVIDIA GeForce RTX 5090" is mostly prefix, and the tile's meta line is short.
const shortGpuName = (n) => (n || '')
  .replace(/^NVIDIA\s+(GeForce\s+)?/i, '')
  .replace(/^AMD\s+/i, '');

// The one power figure the tile shows. Whole-package power when powercap can be
// read; otherwise a GPU's own measured draw, labelled for what it is rather
// than passed off as the machine.
function powerReading() {
  if (sensors.packageWatts >= 0) return { watts: sensors.packageWatts, label: 'package' };
  const g = shownGpus().find((x) => x.powerW >= 1);
  return g ? { watts: g.powerW, label: `${shortGpuName(g.name)} only` } : null;
}

function push(arr, v, keep) {
  arr.push(v);
  while (arr.length > keep) arr.shift();
}

function sample() {
  if (sensors.cpuLoadPct >= 0) push(cpuHistory, sensors.cpuLoadPct, HISTORY);
  for (const g of shownGpus()) {
    if (!gpuHistory.has(g.id)) gpuHistory.set(g.id, []);
    if (g.utilPct >= 0) push(gpuHistory.get(g.id), g.utilPct, HISTORY);
  }
  const p = powerReading();
  if (p) push(powerHistory, p.watts, POWER_HISTORY);
}

const SVG = 'http://www.w3.org/2000/svg';

// height is the viewBox height, floor the smallest full scale. The floor keeps
// an idle machine drawing a flat line near the bottom instead of amplifying
// noise into a mountain range.
function spark(hist, cls, height, floor) {
  const svg = document.createElementNS(SVG, 'svg');
  svg.setAttribute('viewBox', `0 0 100 ${height}`);
  svg.setAttribute('preserveAspectRatio', 'none');
  svg.setAttribute('class', `spark ${cls}`);
  if (hist.length >= 2) {
    const max = Math.max(floor, ...hist);
    const line = document.createElementNS(SVG, 'polyline');
    line.setAttribute('points', hist.map((v, i) =>
      `${(i / (hist.length - 1) * 100).toFixed(2)},${(height - (v / max) * (height - 2)).toFixed(2)}`)
      .join(' '));
    line.setAttribute('vector-effect', 'non-scaling-stroke');
    svg.append(line);
  }
  return svg;
}

function unavailable(c, what, why) {
  add(c, h('div', 'none', what));
  if (why) add(c, h('div', 'why', why));
  return c;
}

function mediaAction(action) {
  api.call('media.control', { action }).catch(() => {});
}

// -------------------------------------------------------------------- tiles

// A byte rate from the agent (MiB/s) as a [number, unit] pair in bits, the way
// network gear is quoted. Whole megabits hid ordinary traffic: a browsing
// session on Wi-Fi is tens of kilobits and read as a flat 0 Mb/s.
function netRate(mbs) {
  const mbit = mbs * 8;
  const kbit = Math.round(mbit * 1024);
  if (kbit < 1000) return [String(kbit), 'Kb/s'];
  if (mbit < 10) return [mbit.toFixed(1), 'Mb/s'];
  return [String(Math.round(mbit)), 'Mb/s'];
}

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
    const parts = [];
    if (sensors.cpuTempC >= 0) parts.push([String(Math.round(sensors.cpuTempC)), 3], '°C');
    if (sensors.cpuCores > 0 && sensors.cpuThreads > 0)
      parts.push(`${parts.length ? ' · ' : ''}${sensors.cpuCores}C/${sensors.cpuThreads}T`);
    if (parts.length) add(head, line('meta', ...parts));
    add(c, head);
    add(c, add(h('div', 'value'),
      fixed(whole(sensors.cpuLoadPct), 3, 'num'), h('span', 'unit', '%')));
    return add(c, add(h('div', 'chart'), spark(cpuHistory, 'cpu', 30, 60)));
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
      const parts = [];
      if (shown[0].tempC >= 0) parts.push([String(Math.round(shown[0].tempC)), 3], '°C');
      if (shown[0].name) parts.push(`${parts.length ? ' · ' : ''}${shortGpuName(shown[0].name)}`);
      if (parts.length) add(head, line('meta', ...parts));
    } else {
      add(head, h('span', 'meta', `${shown.length} cards`));
    }
    add(c, head);

    const wrap = h('div', `gpus${shown.length > 1 ? ' two' : ''}`);
    for (const g of shown) {
      const row = h('div', 'gpu-row');
      add(row, add(h('div', 'value'),
        fixed(whole(g.utilPct), 3, 'num'), h('span', 'unit', '%')));
      if (shown.length > 1) add(row, h('div', 'name', shortGpuName(g.name)));
      add(row, add(h('div', 'chart'), spark(gpuHistory.get(g.id) || [], 'gpu', 30, 60)));
      add(wrap, row);
    }
    return add(c, wrap);
  },

  memory: (cols, rows) => {
    const c = card('stat', cols, rows);
    add(c, h('div', 'kicker', 'MEMORY'));
    const big = add(h('div', 'big'), fixed(sensors.ramTotalGiB ? one(sensors.ramUsedGiB) : '—', 5));
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
    add(c, add(h('div', 'big'), fixed(one(total), 5), h('span', 'unit', ' MB/s')));
    const parts = [];
    if (sensors.diskDevice) parts.push(sensors.diskDevice);
    if (sensors.diskUsedPct >= 0)
      parts.push(parts.length ? ' · ' : '', [String(Math.round(sensors.diskUsedPct)), 3], '% full');
    return add(c, line('sub', ...parts));
  },

  network: (cols, rows) => {
    const c = card('stat', cols, rows);
    add(c, h('div', 'kicker', 'NETWORK'));
    if (!(sensors.netRxMBs >= 0)) {
      add(c, add(h('div', 'big'), fixed('—', 3), h('span', 'unit', ' Mb/s')));
      return add(c, h('div', 'sub', sensors.netInterface || ''));
    }
    const [total, unit] = netRate(sensors.netRxMBs + sensors.netTxMBs);
    const [down, downUnit] = netRate(sensors.netRxMBs);
    const [up, upUnit] = netRate(sensors.netTxMBs);
    add(c, add(h('div', 'big'), fixed(total, 3), h('span', 'unit', ` ${unit}`)));
    // The subline drops the "/s" the big figure already carries: at full
    // width, three-digit rates both ways and an interface name, it is the
    // difference between one line and two. Long interface names (a bridge
    // can be fifteen characters) are cut rather than pushing the rates out.
    const per = (u) => u.replace('/s', '');
    const name = sensors.netInterface;
    return add(c, line('sub',
      name ? add(h('span', 'iface', name)) : '', name ? ' · ' : '',
      '↓ ', [down, 3], ` ${per(downUnit)} · ↑ `, [up, 3], ` ${per(upUnit)}`));
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
    add(c, body);

    // Real MPRIS calls on the player, sent by the agent over the ordinary
    // session connection. The play button shows what pressing it will do.
    const btn = (cls, label, action) => {
      const b = h('div', `btn${cls ? ` ${cls}` : ''}`, label);
      b.addEventListener('click', () => mediaAction(action));
      return b;
    };
    return add(c, add(h('div', 'ctl'),
      btn('', '‹‹', 'previous'),
      btn('play', np.status === 'Playing' ? '❙❙' : '▶', 'playpause'),
      btn('', '››', 'next')));
  },

  power: (cols, rows) => {
    const p = powerReading();
    if (!p) {
      const c = card('stat', cols, rows);
      add(c, h('div', 'kicker', 'POWER DRAW'));
      return unavailable(c, 'no power sensor',
        "powercap's energy counters are readable by root only, and no GPU reports its draw");
    }
    const c = card('power', cols, rows);
    add(c, add(h('div', 'left'),
      h('div', 'kicker', 'POWER DRAW'),
      add(h('div', 'big'), fixed(String(Math.round(p.watts)), 3), h('span', 'unit', ' W')),
      h('div', 'sub', p.label)));
    return add(c, add(h('div', 'chart'), spark(powerHistory, 'power', 40, 300)));
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
      add(wrap, add(h('div', 'item'),
        top, h('div', 'title', n.summary), h('div', 'text', n.body)));
    }
    add(c, wrap);

    const clear = h('div', null, 'CLEAR ALL');
    clear.addEventListener('click', () => { api.call('notifications.clear').catch(() => {}); });
    // Do not disturb would mean telling the notification daemon to be quiet. A
    // monitor connection is forbidden from sending anything at all, so this is
    // shown dimmed rather than as a button that would do nothing.
    return add(c, add(h('div', 'actions'), clear, h('div', 'off', 'DO NOT DISTURB')));
  },
};

// ------------------------------------------------------------------ render

function fit() {
  // Fill both axes rather than letterboxing. The artboard already has the
  // panel's proportions, so the two factors only differ when the window is a
  // pixel off, and letterboxing turned that pixel into a visible band.
  const sx = window.innerWidth / 2560;
  const sy = window.innerHeight / 720;
  board.style.setProperty('--sx', String(sx > 0 ? sx : 1));
  board.style.setProperty('--sy', String(sy > 0 ? sy : 1));
}

function render() {
  const tiles = [];
  for (const [key, cols, rows] of LAYOUTS[layout.page] || LAYOUTS.system) {
    if (layout.tiles[LABEL_OF[key]] === false) continue;
    tiles.push(TILES[key](cols, rows));
  }
  board.replaceChildren(...tiles);
}

api.onEvent((msg) => {
  if (msg.event === 'sensors') {
    sensors = msg.data;
    sample();
    render();
  } else if (msg.event === 'dashboard') {
    layout = { ...layout, ...msg.data };
    if (msg.data && msg.data.userTheme) setUserTheme(msg.data.userTheme);
    if (msg.data && (msg.data.theme || msg.data.userTheme)) applyTheme(msg.data.theme || currentTheme);
    setPage(layout.page);
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
