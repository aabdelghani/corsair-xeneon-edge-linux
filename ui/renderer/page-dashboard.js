// EdgeLine: the Dashboard configuration page.
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';

// The panel's three themes. Only the colours the preview needs are listed; the
// full token sets live in dashboard.css.
const DASH_THEMES = [
  { id: 'night', label: 'Night',
    bg: '#07080A', card: '#101216', line: '#1E2127', text: '#E8EAED', kicker: '#8F969E', accent: '#FF5B1E' },
  { id: 'daylight', label: 'Daylight',
    bg: '#F1EDE6', card: '#FFFCF7', line: '#E4DCD0', text: '#2A2622', kicker: '#6B6459', accent: '#D45C7C' },
  { id: 'porcelain', label: 'Porcelain',
    bg: '#A89878', card: '#BCAE90', line: '#9E8E6C', text: '#221F1C', kicker: '#3A352E', accent: '#8F3A53' },
  { id: 'sage', label: 'Sage',
    bg: '#6F9066', card: '#86A67C', line: '#648460', text: '#161B17', kicker: '#21281E', accent: '#793146' },
];

// The User theme's starting colours, until something is chosen and saved.
const DASH_USER_DEFAULT = { bg: '#07080A', card: '#101216', text: '#E8EAED' };

function dashMix(a, b, t) {
  const pa = [1, 3, 5].map((i) => parseInt(a.slice(i, i + 2), 16));
  const pb = [1, 3, 5].map((i) => parseInt(b.slice(i, i + 2), 16));
  return `#${pa.map((v, i) => Math.round(v + (pb[i] - v) * t).toString(16).padStart(2, '0')).join('')}`;
}

function dashLum(h) {
  const c = [1, 3, 5].map((i) => parseInt(h.slice(i, i + 2), 16) / 255)
    .map((x) => (x <= 0.03928 ? x / 12.92 : ((x + 0.055) / 1.055) ** 2.4));
  return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
}

const dashContrast = (a, b) => {
  const x = dashLum(a);
  const y = dashLum(b);
  return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05);
};

// The fixed themes plus User, whose preview colours are derived the same way
// dashboard.js derives the panel's.
function dashThemeList() {
  const u = dashUi.userTheme;
  return [...DASH_THEMES, {
    id: 'user', label: 'User', bg: u.bg, card: u.card, text: u.text,
    line: dashMix(u.card, u.text, 0.22), kicker: dashMix(u.text, u.card, 0.32), accent: '#FF5B1E',
  }];
}

const dashUi = {
  open: false,
  // Theme, page and tiles are saved by the main process as they change, and
  // loaded from its panel prefs on this page's first render.
  theme: 'night',
  page: 'system',
  userTheme: { ...DASH_USER_DEFAULT },
  prefsLoaded: false,
  // Tile visibility, per key. Unset means shown.
  tiles: {},
  loaded: false,
  // Every GPU in the machine, and the ids the tile is showing. An empty
  // selection means automatic, which is the card with the most VRAM.
  gpus: [],
  gpuIds: [],
  gpusLoaded: false,
  // Network interfaces, and the one chosen. An empty name means automatic.
  ifaces: [],
  iface: '',
  ifacesLoaded: false,
  showVirtual: false,
};

// [tile, columns, rows] on the panel's twelve by three grid. These mirror the
// layouts in dashboard.js, so the preview below is the panel rather than an
// impression of it.
const DASH_PAGES = [
  { id: 'system', label: 'System',
    tiles: [['Clock', 3, 2], ['CPU', 3, 1], ['GPU', 3, 1], ['Notifications', 3, 3],
            ['Memory', 2, 1], ['Disk I/O', 2, 1], ['Network', 2, 1],
            ['Now playing', 5, 1], ['Power', 4, 1]] },
  { id: 'media', label: 'Media',
    tiles: [['Clock', 3, 2], ['Now playing', 6, 2], ['Notifications', 3, 3],
            ['Memory', 3, 1], ['Network', 3, 1], ['Power', 3, 1]] },
];

const dashPageTiles = () => (DASH_PAGES.find((p) => p.id === dashUi.page) || DASH_PAGES[0]).tiles;

const DASH_TILE_META = {
  Clock: ['fa-regular fa-clock', '24h · date · uptime'],
  CPU: ['fa-solid fa-microchip', 'load · temp'],
  GPU: ['fa-solid fa-server', 'nvidia-smi · amdgpu'],
  Memory: ['fa-solid fa-memory', 'used / total'],
  Network: ['fa-solid fa-wifi', 'interface picked below'],
  'Disk I/O': ['fa-solid fa-hard-drive', 'whole device'],
  'Now playing': ['fa-solid fa-music', 'MPRIS player'],
  Notifications: ['fa-regular fa-bell', 'D-Bus monitor'],
  Power: ['fa-solid fa-bolt', 'powercap, else GPU'],
};

function pushDashLayout() {
  api.dashboardLayout({
    tiles: dashUi.tiles, theme: dashUi.theme, page: dashUi.page, userTheme: dashUi.userTheme,
  }).catch(() => {});
}

function setDashPage(id) {
  dashUi.page = id;
  pushDashLayout();   // the main process saves it as it forwards it
  render();
}

function setDashTheme(id) {
  dashUi.theme = id;
  pushDashLayout();   // the main process saves it as it forwards it
  render();
}

async function toggleDashboard() {
  try {
    const r = await api.dashboard(!dashUi.open);
    if (!r.ok) { touchUi.error = r.error; render(); return; }
    dashUi.open = !dashUi.open;
    if (dashUi.open) setTimeout(pushDashLayout, 400);
  } catch { /* ignore */ }
  render();
}

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

function stripPreview() {
  const s = state.sensors || {};
  const t = dashThemeList().find((x) => x.id === dashUi.theme) || DASH_THEMES[0];
  const gpuText = () => {
    const all = s.gpus || [];
    const shown = (s.gpuIdsShown || []).map((id) => all.find((g) => g.id === id)).filter(Boolean);
    const use = shown.length ? shown : (s.gpuOk ? [{ utilPct: s.gpuUtilPct }] : []);
    return use.length ? use.map((g) => `${Math.round(g.utilPct)}%`).join(' · ') : '—';
  };
  const powerText = () => {
    if (s.packageWatts >= 0) return `${Math.round(s.packageWatts)} W`;
    const g = (s.gpus || []).find((x) => x.powerW >= 1);
    return g ? `${Math.round(g.powerW)} W` : '—';
  };
  const value = {
    Clock: ['LOCAL TIME', new Date().toTimeString().slice(0, 5)],
    CPU: ['CPU', s.cpuLoadPct >= 0 ? `${Math.round(s.cpuLoadPct)}%` : '—'],
    GPU: ['GPU', gpuText()],
    Memory: ['MEMORY', s.ramTotalGiB ? `${s.ramUsedGiB.toFixed(1)} GB` : '—'],
    Network: ['NETWORK', s.netRxMBs >= 0 ? netRate(s.netRxMBs + s.netTxMBs).join(' ') : '—'],
    'Disk I/O': ['DISK I/O', s.diskReadMBs >= 0
      ? `${(s.diskReadMBs + s.diskWriteMBs).toFixed(1)} MB/s` : '—'],
    'Now playing': ['NOW PLAYING',
      s.nowPlaying && s.nowPlaying.valid ? s.nowPlaying.title : 'nothing playing'],
    Notifications: ['NOTIFICATIONS', s.notifyActive === false
      ? 'not watching'
      : ((s.notifications || []).length ? `${s.notifications.length} new` : 'nothing new')],
    Power: ['POWER DRAW', powerText()],
  };

  return el('div', {
    style: `border-radius:12px;background:${t.bg};border:1px solid var(--border);`
         + 'aspect-ratio:2560/720;display:grid;gap:8px;padding:12px;'
         + 'max-height:220px;overflow:hidden;'
         + 'grid-template-columns:repeat(12,minmax(0,1fr));grid-template-rows:repeat(3,1fr)',
  },
    ...dashPageTiles().filter(([k]) => dashUi.tiles[k] !== false).map(([k, cols, rows]) => {
      const [kicker, big] = value[k] || ['', '—'];
      return el('div', {
        style: `grid-column:span ${cols};grid-row:span ${rows};`
             + `min-width:0;min-height:0;border-radius:9px;background:${t.card};border:1px solid ${t.line};`
             + 'display:flex;flex-direction:column;justify-content:center;gap:5px;padding:0 12px;overflow:hidden',
      },
        el('div', {
          class: 'mono',
          style: `font-size:10.5px;letter-spacing:.14em;color:${t.kicker};white-space:nowrap;overflow:hidden`,
        }, kicker),
        el('div', {
          style: `font-size:${k === 'Clock' ? 26 : 16}px;font-weight:500;line-height:1.1;color:${t.text};`
               + 'white-space:nowrap;overflow:hidden;text-overflow:ellipsis',
        }, big));
    }));
}

function tileListCard() {
  const tiles = dashPageTiles();
  const shown = tiles.filter(([k]) => dashUi.tiles[k] !== false).length;
  return el('div', { class: 'card', style: 'padding:18px;display:flex;flex-direction:column;gap:12px' },
    el('div', { style: 'display:flex;align-items:baseline;gap:10px' },
      el('div', { class: 'card-kicker' }, 'TILES ON THIS PAGE'),
      el('div', { style: 'margin-left:auto;font-size:12px;color:var(--text4)' },
        `${shown} on`)),
    // Two columns, so the list fits the fixed 760 window without scrolling.
    el('div', { style: 'display:grid;grid-template-columns:1fr 1fr;column-gap:14px' },
      ...tiles.map(([k]) => {
        const [ic, meta] = DASH_TILE_META[k];
        const on = dashUi.tiles[k] !== false;
        return el('div', {
          style: 'display:flex;align-items:center;gap:9px;font-size:13px;'
               + 'cursor:pointer;padding:2px 0;min-width:0',
          onclick: () => { dashUi.tiles[k] = !on; pushDashLayout(); render(); },
        },
          el('div', {
            style: 'width:14px;height:14px;border-radius:4px;flex:none;border:1px solid '
                 + (on ? 'var(--accent)' : 'var(--line)') + (on ? ';background:var(--accent)' : ''),
          }),
          icon(ic, 'width:14px;text-align:center;font-size:11px;color:var(--text4);flex:none'),
          el('div', {
            style: `color:${on ? 'var(--text)' : 'var(--text4)'};white-space:nowrap`,
          }, k),
          el('div', {
            style: 'margin-left:auto;font-size:11px;padding-left:6px;min-width:0;color:var(--text5);'
                 + 'overflow:hidden;text-overflow:ellipsis;white-space:nowrap',
          }, meta));
      })));
}

function setGpuSelection(ids) {
  dashUi.gpuIds = ids;
  render();
  api.call('sensors.selectGpus', { ids }).then((r) => {
    // The agent answers with what it kept, which drops any card that has been
    // removed since the choice was saved.
    dashUi.gpus = r.gpus || dashUi.gpus;
    dashUi.gpuIds = r.selected || [];
    render();
  }).catch(() => {});
}

function setNetInterface(name) {
  dashUi.iface = name;
  render();
  api.call('sensors.selectInterface', { name }).then((r) => {
    dashUi.ifaces = r.interfaces || dashUi.ifaces;
    dashUi.iface = r.selected || '';
    render();
  }).catch(() => {});
}

const bytesText = (b) => (b >= 1024 ** 3 ? `${(b / 1024 ** 3).toFixed(1)} GB`
  : b >= 1024 ** 2 ? `${Math.round(b / 1024 ** 2)} MB` : `${Math.round(b / 1024)} KB`);

function netPickerCard() {
  if (!dashUi.ifaces.length) return null;
  const physical = dashUi.ifaces.filter((n) => !n.virtual);
  const virtual = dashUi.ifaces.filter((n) => n.virtual);
  // A saved virtual choice stays visible even with the list folded, so the
  // selected row is never hidden.
  const shown = [...physical, ...virtual.filter((n) => dashUi.showVirtual || n.name === dashUi.iface)];
  const auto = !dashUi.iface;
  const active = (state.sensors || {}).netInterface;

  const row = (on, label, meta, onclick) => el('div', {
    style: 'display:flex;align-items:center;gap:11px;font-size:14px;cursor:pointer;padding:3px 0',
    onclick,
  },
    el('div', {
      style: 'width:15px;height:15px;border-radius:50%;flex:none;border:1px solid '
           + (on ? 'var(--accent);background:radial-gradient(circle,var(--accent) 45%,transparent 52%)' : 'var(--line)'),
    }),
    el('div', {
      class: 'mono',
      style: `font-size:13px;color:${on ? 'var(--text)' : 'var(--text4)'};min-width:0;`
           + 'overflow:hidden;text-overflow:ellipsis;white-space:nowrap',
    }, label),
    el('div', {
      style: 'margin-left:auto;font-size:12px;color:var(--text5);flex:none;padding-left:10px',
    }, meta));

  return el('div', { class: 'card', style: 'padding:18px;display:flex;flex-direction:column;gap:8px' },
    el('div', { style: 'display:flex;align-items:baseline;gap:10px' },
      el('div', { class: 'card-kicker' }, 'NETWORK TILE'),
      el('div', { class: 'mono', style: 'margin-left:auto;font-size:12px;color:var(--text4)' },
        active ? `reading ${active}` : auto ? 'automatic' : dashUi.iface)),
    el('div', { class: 'card-sub' }, 'Which interface the tile measures.'),
    row(auto, 'Automatic', 'busiest interface that is up', () => setNetInterface('')),
    ...shown.map((n) => row(
      dashUi.iface === n.name,
      n.name,
      [n.up ? 'up' : 'down', n.wireless ? 'Wi-Fi' : n.virtual ? 'virtual' : '',
       bytesText(n.rxBytes + n.txBytes)].filter(Boolean).join(' · '),
      () => setNetInterface(n.name))),
    virtual.length
      ? el('div', {
          style: 'font-size:12px;color:var(--text4);cursor:pointer;padding-top:2px',
          onclick: () => { dashUi.showVirtual = !dashUi.showVirtual; render(); },
        }, dashUi.showVirtual
          ? 'hide bridges, containers and tunnels'
          : `show bridges, containers and tunnels (${virtual.length})`)
      : null);
}

function gpuPickerCard() {
  // Nothing to choose on a single-GPU machine, and a card explaining that
  // would be noise on most of them.
  if (dashUi.gpus.length < 2) return null;

  const auto = dashUi.gpuIds.length === 0;
  const row = (on, label, meta, onclick) => el('div', {
    style: 'display:flex;align-items:center;gap:11px;font-size:14px;cursor:pointer;padding:3px 0',
    onclick,
  },
    el('div', {
      style: 'width:15px;height:15px;border-radius:4px;flex:none;border:1px solid '
           + (on ? 'var(--accent)' : 'var(--line)') + (on ? ';background:var(--accent)' : ''),
    }),
    el('div', {
      style: `color:${on ? 'var(--text)' : 'var(--text4)'};min-width:0;`
           + 'overflow:hidden;text-overflow:ellipsis;white-space:nowrap',
    }, label),
    el('div', {
      style: 'margin-left:auto;font-size:12px;color:var(--text5);flex:none;padding-left:10px',
    }, meta));

  return el('div', { class: 'card', style: 'padding:18px;display:flex;flex-direction:column;gap:8px' },
    el('div', { style: 'display:flex;align-items:baseline;gap:10px' },
      el('div', { class: 'card-kicker' }, 'GPU TILE'),
      el('div', { style: 'margin-left:auto;font-size:12px;color:var(--text4)' },
        auto ? 'automatic' : `${dashUi.gpuIds.length} of ${dashUi.gpus.length}`)),
    // The card only exists when there is more than one GPU, so saying so here
    // would waste the line.
    el('div', { class: 'card-sub' }, 'Show any one of them, or all side by side.'),
    row(auto, 'Automatic', 'the card with the most VRAM', () => setGpuSelection([])),
    ...dashUi.gpus.map((g) => row(
      dashUi.gpuIds.includes(g.id),
      g.name || g.id,
      [g.memTotalGiB >= 0 ? `${Math.round(g.memTotalGiB)} GB` : '', g.source]
        .filter(Boolean).join(' · '),
      () => setGpuSelection(dashUi.gpuIds.includes(g.id)
        ? dashUi.gpuIds.filter((x) => x !== g.id)
        : [...dashUi.gpuIds, g.id]))));
}

// Theme chips sit where the page chips used to, so choosing a theme costs the
// fixed-height page no extra room. Each carries a swatch of its own ground,
// card and accent, which says more than the name does.
function pageChips() {
  return el('div', { style: 'display:flex;align-items:center;gap:8px' },
    el('div', { style: 'font-size:12.5px;color:var(--text4)' }, 'Page'),
    ...DASH_PAGES.map((p) => el('button', {
      class: `chip${dashUi.page === p.id ? ' on' : ''}`,
      style: 'padding:5px 13px;border-radius:14px;font-size:12.5px',
      onclick: () => setDashPage(p.id),
    }, p.label)));
}

function themeChips() {
  // A dropdown now there are five themes: a chip for each no longer fits the
  // fixed-height page's header. The swatch beside it shows the chosen theme's
  // ground, card and accent, since a list of names alone says little.
  const t = dashThemeList().find((x) => x.id === dashUi.theme) || DASH_THEMES[0];
  return el('div', { style: 'display:flex;align-items:center;gap:8px' },
    el('div', { style: 'font-size:12.5px;color:var(--text4)' }, 'Panel theme'),
    el('span', {
      style: `width:34px;height:20px;border-radius:10px;flex:none;background:${t.bg};`
           + `border:1px solid ${t.line};display:flex;align-items:center;gap:3px;`
           + 'justify-content:flex-end;padding-right:4px;box-sizing:border-box',
    },
      el('span', { style: `width:9px;height:9px;border-radius:3px;background:${t.card}` }),
      el('span', { style: `width:9px;height:9px;border-radius:50%;background:${t.accent}` })),
    el('select', {
      style: 'background:var(--sunken);border:1px solid var(--border2);border-radius:12px;'
           + 'padding:4px 10px;color:var(--text);font-family:inherit;font-size:12.5px;outline:none;cursor:pointer',
      // Blur first: the page does not redraw while a control inside it has
      // focus, and the User colour card has to appear as soon as it is picked.
      onchange: (e) => { e.target.blur(); setDashTheme(e.target.value); },
    }, ...dashThemeList().map((x) =>
      el('option', { value: x.id, selected: x.id === dashUi.theme || null }, x.label))));
}

function setUserColor(key, value) {
  dashUi.userTheme = { ...dashUi.userTheme, [key]: value.toLowerCase() };
  pushDashLayout();   // saved by the main process and sent straight to the panel
}

// Shown only while User is the chosen theme. Each colour can be picked, with
// Chromium's chooser offering RGB, HSL and hex fields of its own, or typed as
// hex. The panel follows every valid change live; the readouts say when a
// choice would make text hard to read rather than refusing it.
function userThemeCard() {
  if (dashUi.theme !== 'user') return null;
  const u = dashUi.userTheme;

  const row = (key, label) => el('div', { style: 'display:flex;align-items:center;gap:10px;font-size:13px' },
    el('div', { style: 'width:86px;color:var(--text3)' }, label),
    el('input', {
      type: 'color', value: u[key],
      style: 'width:40px;height:26px;padding:0 2px;border:1px solid var(--border2);'
           + 'border-radius:6px;background:var(--sunken);cursor:pointer',
      oninput: (e) => setUserColor(key, e.target.value),
      onchange: () => render(),
    }),
    el('input', {
      type: 'text', value: u[key].toUpperCase(), maxlength: '7', spellcheck: 'false', class: 'mono',
      style: 'width:88px;background:var(--sunken);border:1px solid var(--border2);border-radius:8px;'
           + 'padding:4px 8px;color:var(--text);font-size:12.5px;outline:none',
      oninput: (e) => {
        const v = e.target.value.trim();
        const hex = v.startsWith('#') ? v : `#${v}`;
        if (/^#[0-9a-fA-F]{6}$/.test(hex)) setUserColor(key, hex);
      },
      onchange: () => render(),
    }));

  const readout = (label, a, b, need) => {
    const r = dashContrast(a, b);
    return el('span', { style: `color:${r >= need ? 'var(--text4)' : '#f99b11'}` },
      `${label} ${r.toFixed(1)}:1${r >= need ? '' : ', hard to read'}`);
  };

  return el('div', { class: 'card', style: 'padding:18px;display:flex;flex-direction:column;gap:9px' },
    el('div', { style: 'display:flex;align-items:baseline;gap:10px' },
      el('div', { class: 'card-kicker' }, 'USER THEME'),
      el('div', { style: 'margin-left:auto;font-size:12px;color:var(--text4)' }, 'saved as you change it')),
    row('bg', 'Background'),
    row('card', 'Cards'),
    row('text', 'Text'),
    el('div', { style: 'display:flex;gap:14px;flex-wrap:wrap;font-size:12px' },
      readout('text on cards', u.text, u.card, 4.5),
      readout('text on background', u.text, u.bg, 4.5)));
}

PAGES.dashboard = (host) => {
  // Before anything on this page can push a layout, so a first click can never
  // overwrite the saved choices with these defaults.
  if (!dashUi.prefsLoaded && state.app && state.app.panelPrefs) {
    dashUi.prefsLoaded = true;
    const p = state.app.panelPrefs;
    const hex = /^#[0-9a-fA-F]{6}$/;
    if (p.userTheme && ['bg', 'card', 'text'].every((k) => hex.test(p.userTheme[k])))
      dashUi.userTheme = { bg: p.userTheme.bg, card: p.userTheme.card, text: p.userTheme.text };
    if (dashThemeList().some((t) => t.id === p.theme)) dashUi.theme = p.theme;
    if (DASH_PAGES.some((pg) => pg.id === p.page)) dashUi.page = p.page;
    if (p.tiles && typeof p.tiles === 'object') dashUi.tiles = { ...p.tiles };
  }
  if (!dashUi.loaded) {
    dashUi.loaded = true;
    api.dashboardState().then((s) => { dashUi.open = s.open; render(); }).catch(() => {});
  }
  if (!dashUi.gpusLoaded) {
    dashUi.gpusLoaded = true;
    // Asked for once per session. This enumerates both vendors in the agent,
    // so it works before anything has started a sensor stream.
    api.call('sensors.gpus').then((r) => {
      dashUi.gpus = r.gpus || [];
      dashUi.gpuIds = r.selected || [];
      render();
    }).catch(() => {
      // Opening this page before the agent has connected fails the call. Let
      // the next render try again rather than leaving the picker missing for
      // the rest of the session.
      dashUi.gpusLoaded = false;
    });
  }
  if (!dashUi.ifacesLoaded) {
    dashUi.ifacesLoaded = true;
    api.call('sensors.interfaces').then((r) => {
      dashUi.ifaces = r.interfaces || [];
      dashUi.iface = r.selected || '';
      render();
    }).catch(() => { dashUi.ifacesLoaded = false; });
  }
  const edge = !!state.app.edgePresent;

  fill(host,
    el('div', { style: 'display:flex;align-items:center;gap:16px;margin-bottom:20px;flex-wrap:wrap' },
      el('div', { class: 'page-title' }, 'Dashboard'),
      el('button', {
        class: `toggle lg${dashUi.open ? ' on' : ''}`,
        disabled: !edge || !state.connected,
        onclick: toggleDashboard,
      }, el('div', { class: 'knob' })),
      el('div', { style: 'font-size:13px;color:var(--text3)' },
        !edge ? 'the Edge is not attached to this session'
              : dashUi.open ? 'running on the panel' : 'stopped'),
      el('div', { style: 'margin-left:auto;display:flex;align-items:center;gap:18px;flex-wrap:wrap' },
        pageChips(), themeChips())),

    stripPreview(),
    el('div', { style: 'height:16px' }),
    el('div', { style: 'display:grid;grid-template-columns:1fr 1fr;gap:16px;align-items:start' },
      tileListCard(),
      el('div', { style: 'display:flex;flex-direction:column;gap:16px;min-width:0' },
        gpuPickerCard(), netPickerCard(), userThemeCard())));
};
