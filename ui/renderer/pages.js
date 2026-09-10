// EdgeLine: page renderers.
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';

/** Standard page header: title plus a monospace kicker. */
function pageHead(title, kicker) {
  return el('div', { class: 'head-row', style: 'margin-bottom:14px' },
    el('div', { class: 'page-title' }, title),
    kicker ? el('div', { class: 'kicker' }, kicker) : null);
}

/** A card that explains, in place, why a control cannot do anything here. */
function unavailableNote(text) {
  return el('div', { class: 'why' }, icon('fa-solid fa-circle-info'), el('span', {}, text));
}

// Placeholder body used until a page is built out. It shows real state rather
// than lorem, so the agent wiring is visible while the page is still stubbed.
function stub(host, title, kicker, lines) {
  fill(host,
    pageHead(title, kicker),
    el('div', { class: 'card', style: 'padding:22px;display:flex;flex-direction:column;gap:10px' },
      el('div', { class: 'card-kicker' }, 'NOT BUILT YET'),
      el('div', { class: 'card-sub' },
        'This page is part of the redesign and is not implemented in this build.'),
      ...lines.map(([k, v]) =>
        el('div', { style: 'display:flex;gap:12px;font-size:13px' },
          el('span', { style: 'color:var(--text4);min-width:120px' }, k),
          el('span', { class: 'mono', style: 'color:var(--text2)' }, v)))));
}

PAGES.picture = (host) => {
  const b = vcp(0x10), c = vcp(0x12), s = vcp(0x87);
  stub(host, 'Picture', 'DDC/CI', [
    ['ddc ready', String(state.ddc.ready)],
    ['panel model', state.ddc.model || '—'],
    ['mccs', state.ddc.mccs || '—'],
    ['features', String((state.ddc.features || []).length)],
    ['brightness', b ? `${b.value} / ${b.max}` : '—'],
    ['contrast', c ? `${c.value} / ${c.max}` : '—'],
    ['sharpness', s ? `${s.value} / ${s.max}` : '—'],
  ]);
};

PAGES.touch = (host) => {
  stub(host, 'Touch', (state.touch.deviceIds || []).length
    ? `XINPUT ID ${state.touch.deviceIds.join(' + ')} · CRSR EDGE TOUCH`
    : 'NO DIGITIZER', [
    ['mode', state.touch.mode || '—'],
    ['devices', (state.touch.deviceIds || []).join(', ') || '—'],
    ['streaming', String(!!state.touch.streaming)],
  ]);
};

PAGES.dashboard = (host) => {
  const s = state.sensors || {};
  stub(host, 'Dashboard', 'PANEL', [
    ['edge attached', String(!!state.app.edgePresent)],
    ['cpu', s.cpuLoadPct >= 0 ? `${s.cpuLoadPct.toFixed(0)}%` : '—'],
    ['memory', s.ramTotalGiB ? `${s.ramUsedGiB.toFixed(1)} / ${s.ramTotalGiB.toFixed(1)} GiB` : '—'],
    ['gpu', s.gpuOk ? `${s.gpuName} ${s.gpuUtilPct}%` : 'nvidia-smi unavailable'],
  ]);
};

PAGES.profiles = (host) => stub(host, 'Profiles', null, []);

function themeCard() {
  return el('div', { class: 'card', style: 'padding:var(--card-pad);display:flex;flex-direction:column;gap:9px' },
    el('div', { class: 'card-kicker' }, 'APPEARANCE'),
    el('div', { style: 'display:flex;align-items:center;gap:14px;flex-wrap:wrap' },
      el('div', { style: 'font-size:14px;color:var(--text2);width:96px;flex:none' }, 'Theme'),
      el('div', { style: 'display:flex;gap:8px;flex-wrap:wrap' },
        ...THEMES.map((t) => el('button', {
          class: `chip${state.theme === t.id ? ' on' : ''}`,
          onclick: () => setTheme(t.id),
        }, t.label)))),
    );
}

PAGES.settings = (host) => {
  const s = stub(host, 'Settings', null, [
    ['version', state.system.version || state.app.version || '—'],
    ['socket', state.system.socket || '—'],
    ['session', state.system.sessionType || '—'],
    ['update', state.update.state || 'idle'],
  ]);
  host.append(el('div', { style: 'height:16px' }), themeCard());
};

PAGES.developer = (host) => {
  stub(host, 'Developer', 'CLI · D-BUS · IMPORT · PACKAGING', [
    ['electron', state.app.electron || '—'],
    ['chromium', state.app.chrome || '—'],
    ['node', state.app.node || '—'],
    ['hidraw', state.device.path || '—'],
    ['hid access', state.device.accessible ? 'ok' : 'denied'],
  ]);
};
