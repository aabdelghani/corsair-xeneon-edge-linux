// EdgeLine: the Profiles page.
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';

// Cached separately from the agent snapshot because the profile list is only
// fetched when it changes, not on every event.
const profileState = { list: [], active: '', directory: '', loaded: false, busy: false, error: '' };

async function loadProfiles() {
  try {
    const r = await api.call('profiles.list');
    profileState.list = r.profiles || [];
    profileState.active = r.active || '';
    profileState.directory = r.directory || '';
    profileState.error = '';
  } catch (err) {
    profileState.error = err.message;
  }
  profileState.loaded = true;
  render();
}

async function profileAction(fn) {
  if (profileState.busy) return;
  profileState.busy = true;
  profileState.error = '';
  render();
  try {
    await fn();
  } catch (err) {
    profileState.error = err.message;
  }
  profileState.busy = false;
  await loadProfiles();
}

// A tiny in-page prompt. A native dialog would be another window to manage and
// the design has no modal for this.
function nameField(placeholder, initial, onSubmit) {
  const input = el('input', {
    type: 'text', value: initial || '', placeholder,
    style: 'flex:1;min-width:0;background:var(--sunken);border:1px solid var(--border2);'
         + 'border-radius:7px;padding:8px 11px;color:var(--text);font-family:inherit;font-size:13px;outline:none',
    onkeydown: (e) => {
      if (e.key === 'Enter') onSubmit(input.value);
      if (e.key === 'Escape') { profileState.editing = null; render(); }
    },
  });
  queueMicrotask(() => { input.focus(); input.select(); });
  return input;
}

function profileRow(p, index, total) {
  const selected = p.name === profileState.active;
  const border = index < total - 1 ? 'border-bottom:1px solid var(--sunken);' : '';
  return el('div', {
    style: `display:flex;align-items:center;gap:12px;padding:15px 18px;cursor:pointer;${border}`
         + (selected ? 'background:var(--sel);' : ''),
    onclick: () => profileAction(() => api.call('profiles.apply', { name: p.name })),
    title: 'Apply this profile',
  },
    el('div', {
      style: 'width:14px;height:14px;border-radius:50%;flex:none;border:2px solid '
           + (selected ? 'var(--accent)' : 'var(--line)')
           + (selected ? ';background:var(--accent)' : ''),
    }),
    el('div', { style: 'font-size:15px;flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap' }, p.name),
    el('div', { class: 'mono', style: 'font-size:12px;color:var(--text4);text-align:right' }, p.summary || '—'));
}

PAGES.profiles = (host) => {
  if (!profileState.loaded && state.connected) loadProfiles();

  const editing = profileState.editing;
  const rows = profileState.list.length
    ? profileState.list.map((p, i) => profileRow(p, i, profileState.list.length))
    : [el('div', { style: 'padding:22px 18px;color:var(--text4);font-size:14px' },
        state.connected ? 'No profiles yet. Save the panel’s current settings as one.'
                        : 'The agent is not running.')];

  const buttons = el('div', { style: 'display:flex;gap:10px;flex-wrap:wrap;align-items:center' },
    editing
      ? el('div', { style: 'display:flex;gap:10px;flex:1;min-width:0;align-items:center' },
          nameField(editing.mode === 'new' ? 'Name for this profile' : 'New name',
            editing.mode === 'rename' ? profileState.active : '',
            (value) => {
              const name = value.trim();
              if (!name) return;
              profileState.editing = null;
              profileAction(() => editing.mode === 'new'
                ? api.call('profiles.save', { name })
                : api.call('profiles.rename', { from: profileState.active, to: name }));
            }),
          el('button', { class: 'btn btn-small', onclick: () => { profileState.editing = null; render(); } }, 'Cancel'))
      : [
          el('button', {
            class: 'btn btn-small', disabled: !state.connected || profileState.busy,
            onclick: () => { profileState.editing = { mode: 'new' }; render(); },
          }, 'New from current'),
          el('button', {
            class: 'btn btn-small',
            disabled: !profileState.active || profileState.busy,
            onclick: () => { profileState.editing = { mode: 'rename' }; render(); },
          }, 'Rename'),
          el('button', {
            class: 'btn btn-small',
            disabled: !profileState.active || profileState.busy,
            onclick: () => profileAction(() => api.call('profiles.delete', { name: profileState.active })),
          }, 'Delete'),
        ]);

  fill(host,
    pageHead('Profiles'),
    profileState.error
      ? el('div', { class: 'banner warn', style: 'margin:0 0 16px' },
          icon('fa-solid fa-triangle-exclamation'), el('span', {}, profileState.error))
      : null,
    el('div', { class: 'card', style: 'overflow:hidden;margin-bottom:20px' }, ...rows),
    buttons,
    el('div', { style: 'height:20px' }),
    el('div', {
      class: 'card',
      style: 'border-style:dashed;border-color:var(--border2);padding:18px;display:flex;flex-direction:column;gap:8px',
    },
      el('div', { class: 'card-kicker' }, 'A PROFILE STORES'),
      el('div', { style: 'font-size:14px;line-height:1.7;color:var(--text3)' },
        'picture values · preset · RGB gain · touch mode · calibration matrix'),
      // Two things a profile deliberately leaves alone, because restoring them
      // can leave you looking at a black panel with no obvious way back.
      el('div', { class: 'card-sub' },
        'Input source and panel power are not stored: restoring a profile should never '
        + 'switch your input or blank the display.'),
      profileState.directory
        ? el('div', { class: 'mono', style: 'font-size:11.5px;color:var(--text5);margin-top:4px' },
            profileState.directory)
        : null));
};
