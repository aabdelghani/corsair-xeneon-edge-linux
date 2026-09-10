// EdgeLine: the Settings page.
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';

/** One row of the settings list: label on the left, control on the right. */
function settingRow(label, control, opts = {}) {
  return el('div', {
    style: 'display:flex;justify-content:space-between;align-items:center;gap:16px;'
         + 'padding:9px 16px;font-size:13.5px;color:var(--text2);'
         + (opts.last ? '' : 'border-bottom:1px solid var(--sunken);'),
  },
    el('div', { style: 'display:flex;flex-direction:column;gap:2px;min-width:0' },
      el('div', {}, label),
      opts.note ? el('div', { style: 'font-size:11.5px;color:var(--text5);line-height:1.35' }, opts.note) : null),
    control);
}

const valueText = (text, colour) =>
  el('div', { style: `font-size:13px;color:${colour || 'var(--text3)'};white-space:nowrap` }, text);
const valueMono = (text) =>
  el('div', { class: 'mono', style: 'font-size:12px;color:var(--text3);white-space:nowrap' }, text);

function toggle(on, onclick, opts = {}) {
  return el('button', {
    class: `toggle${on ? ' on' : ''}${opts.lg ? ' lg' : ''}`,
    disabled: !!opts.disabled,
    onclick,
  }, el('div', { class: 'knob' }));
}

function settingsListCard() {
  const sys = state.system;
  const bus = sys.i2cBus >= 0 ? `auto (i2c-${sys.i2cBus})` : 'auto (not found)';
  const active = (typeof profileState !== 'undefined' && profileState.active) || '';

  return el('div', { class: 'card', style: 'overflow:hidden' },
    settingRow('Start on login',
      toggle(!!state.autostart, async () => {
        const r = await api.autostartSet(!state.autostart);
        if (r && r.ok) state.autostart = r.enabled;
        render();
      }),
      { note: 'Opens EdgeLine in the tray and starts the agent with it.' }),

    settingRow('Apply profile at startup',
      valueText(active || 'none'),
      { note: active
          ? 'The agent restores the saved touch mode. Picture values are held by the panel itself.'
          : 'Save a profile to use this.' }),

    settingRow('ddcutil path', valueMono(state.system.ddcutilPath || 'not found'),
      { note: state.system.ddcutilPath ? null : 'Picture control needs ddcutil on PATH.' }),

    settingRow('I2C bus', valueText(bus),
      { note: 'Found by matching the panel’s model string, because the bus number moves between reboots.' }),

    settingRow('Global hotkeys',
      valueText('not available', 'var(--text5)'),
      { note: 'Not implemented in this build.', last: true }));
}

function updatesCard() {
  const u = state.update || { state: 'idle' };
  const version = state.system.version || state.app.version || '—';
  const codename = state.system.codename || '';
  const available = u.state === 'available';
  const checking = u.state === 'checking';

  const badgeText = available ? 'update available' : checking ? 'checking'
    : u.state === 'failed' ? 'check failed' : 'up to date';

  return el('div', { class: 'card', style: 'padding:var(--card-pad);display:flex;flex-direction:column;gap:9px' },
    el('div', { style: 'display:flex;align-items:center;gap:12px;flex-wrap:wrap' },
      el('div', { class: 'card-kicker' }, 'UPDATES'),
      el('div', {
        style: 'font-size:11.5px;padding:2px 9px;border-radius:11px;'
             + (available
                 ? 'background:var(--accent);color:var(--on-accent)'
                 : 'border:1px solid var(--border2);color:var(--text3)'),
      }, badgeText),
      el('div', { style: 'margin-left:auto;font-size:12px;color:var(--text5)' },
        checking ? 'contacting release feed…' : u.error ? u.error : '')),

    el('div', { style: 'display:flex;align-items:center;gap:16px;flex-wrap:wrap' },
      el('div', { style: 'display:flex;flex-direction:column;gap:5px;min-width:0' },
        el('div', { style: 'font-size:14px' },
          `EdgeLine ${version} `,
          codename ? el('span', { style: 'color:var(--text3)' }, `“${codename}”`) : null),
        el('div', { class: 'card-sub' },
          available ? `${u.version} is available` : 'the newest stable release')),
      el('div', { style: 'margin-left:auto;display:flex;align-items:center;gap:12px' },
        el('button', {
          class: `btn btn-small${available ? ' btn-accent' : ''}`,
          disabled: !state.connected || checking,
          onclick: () => {
            if (available && u.url) { api.openExternal(u.url); return; }
            state.update = { state: 'checking' };
            render();
            api.call('updates.check', { manual: true }).catch(() => {});
          },
        }, icon('fa-solid fa-arrows-rotate', 'margin-right:7px;font-size:12px'),
           available ? 'Open release' : checking ? 'Checking…' : 'Check for updates'))),

    available && u.version
      ? el('div', { style: 'border-top:1px solid var(--border);padding-top:12px;display:flex;flex-direction:column;gap:7px' },
          el('div', { style: 'font-size:13px;color:var(--text3)' }, `${u.version} — release notes`),
          el('div', { style: 'font-size:12px;color:var(--text5)' },
            'Install it the way you installed this one.'))
      : null,

    el('div', { style: 'border-top:1px solid var(--border);padding-top:12px;display:flex;align-items:center;gap:16px' },
      el('div', { style: 'font-size:14px;color:var(--text2)' }, 'Check automatically on launch'),
      el('div', { style: 'margin-left:auto' },
        toggle(state.system.updatesEnabled !== false, async () => {
          try {
            await api.call('updates.settings', { enabled: !(state.system.updatesEnabled !== false) });
            await refreshAll();
          } catch { /* ignore */ }
        }, { lg: true, disabled: !state.connected })),
      el('div', { style: 'font-size:11.5px;color:var(--text5);flex-basis:100%' },
        'One request to github.com a day. Nothing is downloaded or installed.')));
}

function configCard() {
  const sys = state.system;
  return el('div', {
    class: 'card',
    style: 'border-style:dashed;border-color:var(--border2);padding:var(--card-pad);display:flex;flex-direction:column;gap:6px',
  },
    el('div', { class: 'card-kicker' }, 'CONFIG'),
    el('div', { class: 'mono', style: 'font-size:12.5px;line-height:1.6;color:var(--text3);word-break:break-all' },
      el('div', {}, sys.configPath || '—'),
      el('div', {}, sys.profilesPath || '—'),
      el('div', {}, sys.hidLog || '—')));
}

PAGES.settings = (host) => {
  fill(host,
    pageHead('Settings'),
    settingsListCard(),
    el('div', { style: 'height:var(--gap-section)' }),
    updatesCard(),
    el('div', { style: 'height:var(--gap-section)' }),
    themeCard(),
    el('div', { style: 'height:var(--gap-section)' }),
    configCard());
};
