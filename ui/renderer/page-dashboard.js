// EdgeLine: the Dashboard configuration page.
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';

const dashUi = {
  open: false,
  page: 0,
  // Tile visibility, per key. Unset means shown.
  tiles: {},
  loaded: false,
};

const DASH_PAGES = [
  { label: 'System', keys: ['Clock', 'CPU', 'GPU', 'Memory', 'Disk I/O', 'systemd units'] },
  { label: 'Media', keys: ['Clock', 'Now playing', 'Notifications', 'Network'] },
];

const DASH_TILE_META = {
  Clock: ['fa-regular fa-clock', '24h · date', true],
  CPU: ['fa-solid fa-microchip', 'load · temp', true],
  GPU: ['fa-solid fa-server', 'nvidia-smi', true],
  Memory: ['fa-solid fa-memory', 'used / total', true],
  Network: ['fa-solid fa-wifi', 'busiest interface', true],
  'Disk I/O': ['fa-solid fa-hard-drive', 'whole device', true],
  'systemd units': ['fa-solid fa-diagram-project', 'systemctl --failed', true],
  'Now playing': ['fa-solid fa-music', 'needs an MPRIS reader', false],
  Notifications: ['fa-regular fa-bell', 'needs a D-Bus monitor', false],
};

function pushDashLayout() {
  api.dashboardLayout({ page: dashUi.page, tiles: dashUi.tiles }).catch(() => {});
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

function stripPreview() {
  const page = DASH_PAGES[dashUi.page];
  const keys = page.keys.filter((k) => dashUi.tiles[k] !== false);
  const span = (k) => (k === 'Clock' || k === 'Now playing' || k === 'Notifications' ? 2 : 1);
  const cols = keys.reduce((n, k) => n + span(k), 0) || 1;
  const s = state.sensors || {};
  const value = {
    Clock: ['', new Date().toTimeString().slice(0, 5)],
    CPU: ['CPU', s.cpuLoadPct >= 0 ? `${Math.round(s.cpuLoadPct)}%` : '—'],
    GPU: ['GPU', s.gpuOk ? `${Math.round(s.gpuUtilPct)}%` : '—'],
    Memory: ['MEM', s.ramTotalGiB ? `${s.ramUsedGiB.toFixed(1)} GB` : '—'],
    Network: ['NET', s.netRxMBs >= 0 ? `↓ ${s.netRxMBs.toFixed(1)}` : '—'],
    'Disk I/O': ['DISK', s.diskReadMBs >= 0
      ? `${(s.diskReadMBs + s.diskWriteMBs).toFixed(1)} MB/s` : '—'],
    'systemd units': ['UNITS', s.failedUnits >= 0
      ? (s.failedUnits ? `${s.failedUnits} failed` : 'all ok') : '—'],
    'Now playing': ['NOW PLAYING', 'not available'],
    Notifications: ['NOTIFY', 'not available'],
  };

  return el('div', {
    style: 'border-radius:12px;background:var(--strip);border:1px solid var(--border);'
         + 'aspect-ratio:2560/720;display:grid;grid-auto-rows:1fr;gap:10px;padding:14px;'
         + `max-height:260px;overflow:hidden;grid-template-columns:repeat(${cols},minmax(0,1fr))`,
  },
    ...keys.map((k) => {
      const [kicker, big] = value[k] || ['', '—'];
      const supported = DASH_TILE_META[k][2];
      return el('div', {
        style: `grid-column:span ${span(k)};min-width:0;border-radius:9px;background:var(--card);`
             + 'display:flex;flex-direction:column;justify-content:center;gap:5px;padding:0 14px;overflow:hidden',
      },
        kicker ? el('div', {
          class: 'mono',
          style: 'font-size:12px;letter-spacing:.06em;color:var(--text4)',
        }, kicker) : null,
        el('div', {
          style: `font-size:${k === 'Clock' ? 26 : 18}px;font-weight:500;line-height:1.1;`
               + `color:${supported ? 'var(--text)' : 'var(--text4)'};`
               + 'white-space:nowrap;overflow:hidden;text-overflow:ellipsis',
        }, big));
    }));
}

function tileListCard() {
  const page = DASH_PAGES[dashUi.page];
  const shown = page.keys.filter((k) => dashUi.tiles[k] !== false).length;
  return el('div', { class: 'card', style: 'padding:18px;display:flex;flex-direction:column;gap:12px' },
    el('div', { style: 'display:flex;align-items:baseline;gap:10px' },
      el('div', { class: 'card-kicker' }, 'TILES ON THIS PAGE'),
      el('div', { style: 'margin-left:auto;font-size:12px;color:var(--text4)' },
        `${shown} on`)),
    ...page.keys.map((k) => {
      const [ic, meta, supported] = DASH_TILE_META[k];
      const on = dashUi.tiles[k] !== false;
      return el('div', {
        style: 'display:flex;align-items:center;gap:11px;font-size:14px;cursor:pointer;padding:3px 0',
        onclick: () => { dashUi.tiles[k] = !on; pushDashLayout(); render(); },
      },
        el('div', {
          style: 'width:15px;height:15px;border-radius:4px;flex:none;border:1px solid '
               + (on ? 'var(--accent)' : 'var(--line)') + (on ? ';background:var(--accent)' : ''),
        }),
        icon(ic, 'width:16px;text-align:center;font-size:12px;color:var(--text4)'),
        el('div', { style: `color:${on ? 'var(--text)' : 'var(--text4)'}` }, k),
        el('div', {
          style: `margin-left:auto;font-size:12px;color:${supported ? 'var(--text5)' : '#f99b11'}`,
        }, meta));
    }));
}

PAGES.dashboard = (host) => {
  if (!dashUi.loaded) {
    dashUi.loaded = true;
    api.dashboardState().then((s) => { dashUi.open = s.open; render(); }).catch(() => {});
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
      el('div', { style: 'margin-left:auto;display:flex;align-items:center;gap:8px' },
        el('div', { style: 'font-size:12.5px;color:var(--text4)' }, 'Pages'),
        ...DASH_PAGES.map((p, i) => el('button', {
          class: `chip${dashUi.page === i ? ' on' : ''}`,
          style: 'padding:5px 13px;border-radius:14px;font-size:12.5px',
          onclick: () => { dashUi.page = i; pushDashLayout(); render(); },
        }, p.label)))),

    stripPreview(),
    el('div', { style: 'height:16px' }),
    el('div', { style: 'display:grid;grid-template-columns:1fr 1fr;gap:16px' },
      tileListCard(),
      el('div', {
        class: 'card',
        style: 'border-style:dashed;border-color:var(--border2);padding:18px;'
             + 'display:flex;flex-direction:column;gap:10px',
      },
        el('div', { class: 'card-kicker' }, 'WIDGET SDK'),
        el('div', { class: 'card-sub' },
          'The design proposes dropping a folder with tile.html and tile.json into '
          + '~/.local/share/edgeline/tiles/ and having it appear here.'),
        unavailableNote('Not implemented. Loading arbitrary HTML into the panel window '
          + 'needs a sandbox story first, and shipping one without it would be worse '
          + 'than not shipping it.'))));
};
