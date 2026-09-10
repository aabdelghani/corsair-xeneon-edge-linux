// EdgeLine: the per-app rules dialog.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Edits a working copy and only sends it on Save, so cancelling really does
// discard. Rules move the panel behind your back, and a dialog that half-applied
// as you typed would be an unpleasant way to find that out.
'use strict';

const rulesEditor = { open: false, draft: null, error: '', busy: false };

function openRulesModal() {
  const src = state.rules || {};
  rulesEditor.draft = {
    enabled: !!src.enabled,
    restoreOnUnfocus: src.restoreOnUnfocus !== false,
    fallbackProfile: src.fallbackProfile || '',
    rules: (src.rules || []).map((r) => ({ ...r })),
  };
  rulesEditor.error = '';
  rulesEditor.open = true;
  render();
}

function closeRulesModal() {
  rulesEditor.open = false;
  rulesEditor.draft = null;
  render();
}

async function saveRules() {
  if (rulesEditor.busy) return;
  rulesEditor.busy = true;
  rulesEditor.error = '';
  render();
  try {
    state.rules = await api.call('rules.set', rulesEditor.draft);
    rulesEditor.open = false;
    rulesEditor.draft = null;
  } catch (err) {
    rulesEditor.error = err.message;
  }
  rulesEditor.busy = false;
  render();
}

function profileNames() {
  return (typeof profileState !== 'undefined' ? profileState.list : []).map((p) => p.name);
}

/** A <select> of profiles. Free text would let you name one that isn't there. */
function profileSelect(value, onChange, opts = {}) {
  const names = profileNames();
  const sel = el('select', {
    style: 'background:var(--sunken);border:1px solid var(--border2);border-radius:13px;'
         + 'padding:4px 10px;color:var(--text2);font-family:inherit;font-size:12.5px;outline:none;'
         + 'max-width:150px',
    onchange: (e) => onChange(e.target.value),
  });
  if (opts.allowNone) sel.append(el('option', { value: '' }, opts.noneLabel || 'leave alone'));
  for (const n of names) sel.append(el('option', { value: n, selected: n === value || null }, n));
  if (value && !names.includes(value))
    sel.append(el('option', { value, selected: true }, `${value} (missing)`));
  return sel;
}

function ruleRow(rule, index, total) {
  const d = rulesEditor.draft;
  const initial = (rule.app || rule.pattern || '?').trim().charAt(0).toUpperCase() || '?';
  return el('div', {
    style: 'display:grid;grid-template-columns:20px minmax(0,1.4fr) minmax(0,1fr) minmax(0,1fr) 24px;'
         + 'gap:12px;align-items:center;padding:12px 14px;'
         + (index < total - 1 ? 'border-bottom:1px solid var(--border);' : ''),
  },
    el('div', { class: 'mono', style: 'font-size:12px;color:var(--text4)' }, String(index + 1)),

    el('div', { style: 'display:flex;align-items:center;gap:10px;min-width:0' },
      el('div', {
        style: 'width:26px;height:26px;border-radius:7px;background:var(--sunken);color:var(--text3);'
             + 'font-size:13px;display:flex;align-items:center;justify-content:center;flex:none',
      }, initial),
      el('div', { style: 'min-width:0;display:flex;flex-direction:column;gap:2px' },
        el('input', {
          type: 'text', value: rule.app || '', placeholder: 'name',
          style: 'background:none;border:none;color:var(--text);font-size:14px;font-family:inherit;'
               + 'outline:none;padding:0;width:100%',
          oninput: (e) => { rule.app = e.target.value; },
        }),
        el('input', {
          type: 'text', value: rule.pattern || '', placeholder: 'set a match…',
          class: 'mono',
          style: 'background:none;border:none;color:var(--text5);font-size:11px;font-family:inherit;'
               + 'outline:none;padding:0;width:100%',
          oninput: (e) => { rule.pattern = e.target.value; },
        }))),

    el('select', {
      style: 'background:none;border:none;color:var(--text3);font-size:12.5px;font-family:inherit;outline:none',
      onchange: (e) => { rule.matchOn = e.target.value; },
    },
      el('option', { value: 'WM_CLASS', selected: rule.matchOn !== 'window state' || null }, 'WM_CLASS'),
      el('option', { value: 'window state', selected: rule.matchOn === 'window state' || null }, 'window state')),

    profileSelect(rule.profile, (v) => { rule.profile = v; }),

    el('button', {
      style: 'background:none;border:none;color:var(--text4);font-size:14px;cursor:pointer;padding:0',
      title: 'Remove this rule',
      onclick: () => { d.rules.splice(index, 1); render(); },
    }, '✕'));
}

function rulesModal() {
  const d = rulesEditor.draft;
  if (!d) return null;
  const focused = (state.rules && state.rules.focused) || {};

  const header = el('div', {
    style: 'display:flex;align-items:center;gap:12px;height:48px;background:var(--header);padding:0 18px;flex:none',
  },
    el('div', { style: 'font-size:14px;font-weight:500;color:var(--text)' }, 'Per-app rules'),
    el('div', { style: 'font-size:12px;color:var(--text4)' }, 'applied on window focus'),
    el('button', {
      style: 'margin-left:auto;width:24px;height:24px;border-radius:50%;background:var(--border);'
           + 'color:var(--text2);border:none;cursor:pointer',
      onclick: closeRulesModal,
    }, '✕'));

  const columns = el('div', {
    class: 'mono',
    style: 'display:grid;grid-template-columns:20px minmax(0,1.4fr) minmax(0,1fr) minmax(0,1fr) 24px;'
         + 'gap:12px;font-size:10.5px;letter-spacing:.1em;color:var(--text4);padding:0 18px',
  },
    el('div', {}, '#'), el('div', {}, 'WHEN FOCUSED'),
    el('div', {}, 'MATCH ON'), el('div', {}, 'APPLY'), el('div', {}));

  const table = el('div', { style: 'border:1px solid var(--border);border-radius:10px;overflow:hidden' },
    ...(d.rules.length
      ? d.rules.map((r, i) => ruleRow(r, i, d.rules.length))
      : [el('div', { style: 'padding:20px;color:var(--text4);font-size:13.5px' }, 'No rules yet.')]));

  const addRule = (app, pattern, matchOn) => {
    d.rules.push({
      app: app || 'New rule',
      pattern: pattern || '',
      matchOn: matchOn || 'WM_CLASS',
      profile: profileNames()[0] || '',
      enabled: true,
    });
    render();
  };

  const actions = el('div', { style: 'display:flex;gap:10px;align-items:center;flex-wrap:wrap' },
    el('button', {
      class: 'btn btn-small', style: 'border-style:dashed',
      onclick: () => addRule(),
    }, '+ Add rule'),
    // Uses the window the agent currently sees focused, which is why the
    // watcher runs even when rules are switched off.
    el('button', {
      class: 'btn btn-small',
      disabled: !focused.valid,
      title: focused.valid ? `Currently focused: ${focused.wmClassFull}` : 'Nothing focused',
      onclick: () => addRule(focused.wmClass || focused.wmInstance, focused.wmClassFull, 'WM_CLASS'),
    }, focused.valid ? `Use focused window (${focused.wmClassFull})` : 'Pick a window…'),
    el('div', { style: 'margin-left:auto;font-size:12px;color:var(--text5)' }, 'first match wins'));

  const fallback = el('div', {
    style: 'border-top:1px solid var(--border);padding-top:14px;display:flex;align-items:center;gap:14px;flex-wrap:wrap',
  },
    el('div', { style: 'font-size:13.5px;color:var(--text2)' }, 'When nothing matches'),
    profileSelect(d.fallbackProfile, (v) => { d.fallbackProfile = v; },
      { allowNone: true, noneLabel: 'leave the panel alone' }),
    el('div', { style: 'margin-left:auto;display:flex;align-items:center;gap:10px' },
      el('div', { style: 'font-size:13px;color:var(--text3)' }, 'Restore previous on unfocus'),
      el('button', {
        class: `toggle${d.restoreOnUnfocus ? ' on' : ''}`,
        onclick: () => { d.restoreOnUnfocus = !d.restoreOnUnfocus; render(); },
      }, el('div', { class: 'knob' }))));

  const enableRow = el('div', {
    style: 'display:flex;align-items:center;gap:14px;padding-top:4px',
  },
    el('div', { style: 'font-size:13.5px;color:var(--text2)' }, 'Apply rules on focus'),
    el('button', {
      class: `toggle${d.enabled ? ' on' : ''}`,
      onclick: () => { d.enabled = !d.enabled; render(); },
    }, el('div', { class: 'knob' })),
    el('div', { style: 'font-size:12px;color:var(--text5)' },
      d.enabled ? 'The panel will change when you switch windows.'
                : 'Rules are saved but nothing fires.'));

  const footer = el('div', {
    style: 'padding:14px 20px;background:var(--sidebar);border-top:1px solid var(--border);'
         + 'display:flex;gap:10px;justify-content:flex-end;align-items:center',
  },
    rulesEditor.error
      ? el('div', { style: 'margin-right:auto;font-size:12.5px;color:#f99b11' }, rulesEditor.error)
      : null,
    el('button', { class: 'btn btn-small', onclick: closeRulesModal }, 'Cancel'),
    el('button', {
      class: 'btn btn-small btn-accent', disabled: rulesEditor.busy, onclick: saveRules,
    }, rulesEditor.busy ? 'Saving…' : 'Save rules'));

  return el('div', {
    style: 'position:fixed;inset:0;background:rgba(0,0,0,.5);display:flex;align-items:center;'
         + 'justify-content:center;padding:60px;z-index:50',
    onclick: (e) => { if (e.target === e.currentTarget) closeRulesModal(); },
  },
    el('div', {
      style: 'width:780px;max-width:100%;max-height:100%;background:var(--bg);'
           + 'border:1px solid var(--shell-border);border-radius:12px;overflow:hidden;'
           + 'display:flex;flex-direction:column;box-shadow:0 30px 80px rgba(0,0,0,.5)',
    },
      header,
      el('div', { style: 'padding:20px;display:flex;flex-direction:column;gap:14px;overflow-y:auto' },
        columns, table, actions, enableRow, fallback),
      footer));
}
