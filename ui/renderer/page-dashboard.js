// EdgeLine: the Dashboard configuration page.
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';

const dashUi = {
  open: false,
  page: 0,
  // Tile visibility, per key. Unset means shown.
  tiles: {},
  loaded: false,
  // Every GPU in the machine, and the ids the tile is showing. An empty
  // selection means automatic, which is the card with the most VRAM.
  gpus: [],
  gpuIds: [],
  gpusLoaded: false,
};

const DASH_PAGES = [
  { label: 'System', keys: ['Clock', 'CPU', 'GPU', 'Memory', 'Disk I/O', 'systemd units'] },
  { label: 'Media', keys: ['Clock', 'Now playing', 'Notifications', 'Network'] },
];

const DASH_TILE_META = {
  Clock: ['fa-regular fa-clock', '24h · date', true],
  CPU: ['fa-solid fa-microchip', 'load · temp', true],
  GPU: ['fa-solid fa-server', 'nvidia-smi · amdgpu', true],
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
  const span = (k) => (k === 'Clock' || k === 'Now playing' || k === 'Notifications' ? 2 : 1);
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

  // GPU is one tile per selected card, so the preview expands it the same way
  // the panel does rather than always drawing a single box.
  const entries = [];
  for (const k of page.keys) {
    if (dashUi.tiles[k] === false) continue;
    if (k === 'GPU') {
      const ids = dashUi.gpuIds.length ? dashUi.gpuIds : [null];
      ids.forEach((id, i) => {
        const g = (s.gpus || []).find((x) => x.id === id);
        const util = g ? g.utilPct : (s.gpuOk ? s.gpuUtilPct : -1);
        entries.push({
          key: k,
          kicker: ids.length > 1 ? `GPU ${i + 1}` : 'GPU',
          big: util >= 0 ? `${Math.round(util)}%` : '—',
        });
      });
      continue;
    }
    const [kicker, big] = value[k] || ['', '—'];
    entries.push({ key: k, kicker, big });
  }
  const cols = entries.reduce((n, e) => n + span(e.key), 0) || 1;

  return el('div', {
    style: 'border-radius:12px;background:var(--strip);border:1px solid var(--border);'
         + 'aspect-ratio:2560/720;display:grid;grid-auto-rows:1fr;gap:10px;padding:14px;'
         + `max-height:260px;overflow:hidden;grid-template-columns:repeat(${cols},minmax(0,1fr))`,
  },
    ...entries.map((e) => {
      const supported = DASH_TILE_META[e.key][2];
      return el('div', {
        style: `grid-column:span ${span(e.key)};min-width:0;border-radius:9px;background:var(--card);`
             + 'display:flex;flex-direction:column;justify-content:center;gap:5px;padding:0 14px;overflow:hidden',
      },
        e.kicker ? el('div', {
          class: 'mono',
          style: 'font-size:12px;letter-spacing:.06em;color:var(--text4)',
        }, e.kicker) : null,
        el('div', {
          style: `font-size:${e.key === 'Clock' ? 26 : 18}px;font-weight:500;line-height:1.1;`
               + `color:${supported ? 'var(--text)' : 'var(--text4)'};`
               + 'white-space:nowrap;overflow:hidden;text-overflow:ellipsis',
        }, e.big));
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
    // would waste the line. A third card has to fit in a fixed-height page.
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

PAGES.dashboard = (host) => {
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
    el('div', { style: 'display:grid;grid-template-columns:1fr 1fr;gap:16px;align-items:start' },
      tileListCard(),
      el('div', { style: 'display:flex;flex-direction:column;gap:16px;min-width:0' },
        gpuPickerCard(),
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
          + 'than not shipping it.')))));
};
