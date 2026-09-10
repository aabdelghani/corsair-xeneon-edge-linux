// EdgeLine: the Touch page.
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';

const TOUCH_MODES = [
  { id: 'off', label: 'Off', glyph: 'no input', icon: 'fa-solid fa-ban',
    desc: 'Digitizer disabled in xinput. The panel is display-only.' },
  { id: 'main-cursor', label: 'Main cursor', glyph: '↖ shared', icon: 'fa-solid fa-arrow-pointer',
    desc: 'Mapped to the Edge only; your pointer jumps there on touch.' },
  { id: 'own-pointer', label: 'Own pointer', glyph: '↖ + ↖ split', icon: 'fa-solid fa-clone',
    desc: 'A floating master device. Your main cursor never moves.' },
  { id: 'ripple', label: 'Ripple only', glyph: '◎ feedback', icon: 'fa-regular fa-circle-dot',
    desc: 'No pointer at all. Touches just draw a ripple on the panel.' },
];

const touchUi = { busy: false, error: '', points: new Map(), lastCal: null, lastGesture: null, gesturesOpen: false };

async function startCalibration() {
  touchUi.error = '';
  try {
    const r = await api.openCalibration();
    if (!r.ok) touchUi.error = r.error || 'could not open the calibration window';
  } catch (err) {
    touchUi.error = err.message;
  }
  render();
}

async function setTouchMode(id) {
  if (touchUi.busy) return;
  touchUi.busy = true;
  touchUi.error = '';
  render();
  try {
    state.touch = await api.call('touch.setMode', { mode: id });
  } catch (err) {
    touchUi.error = err.message;
  }
  touchUi.busy = false;
  render();
}

function modeCard(m) {
  const selected = state.touch.mode === m.id;
  return el('div', {
    style: 'border-radius:12px;padding:12px 14px;cursor:pointer;background:var(--card);'
         + `border:1px solid ${selected ? 'var(--accent)' : 'var(--border)'};`
         + (touchUi.busy ? 'opacity:.6;pointer-events:none;' : ''),
    onclick: () => setTouchMode(m.id),
  },
    el('div', { style: 'display:flex;align-items:center;gap:10px' },
      el('div', {
        style: 'width:14px;height:14px;border-radius:50%;flex:none;border:2px solid '
             + (selected ? 'var(--accent)' : 'var(--line)')
             + (selected ? ';background:var(--accent)' : ''),
      }),
      icon(m.icon, 'font-size:13px;color:var(--text3)'),
      el('div', { style: 'font-size:16px;color:var(--text)' }, m.label)),
    el('div', { style: 'font-size:12.5px;line-height:1.45;color:var(--text3);margin-top:7px' }, m.desc));
}

function calibrationCard() {
  const cal = touchUi.lastCal;
  const isX11 = (state.system.sessionType || '').toLowerCase() === 'x11';
  return el('div', { class: 'card', style: 'padding:var(--card-pad);display:flex;align-items:center;gap:14px;flex-wrap:wrap' },
    el('div', { style: 'display:flex;flex-direction:column;gap:5px;min-width:0' },
      el('div', { style: 'font-size:16px' }, 'Calibration'),
      el('div', { style: 'font-size:13px;color:var(--text3)' },
        cal
          ? `Last run: residual ${cal.rmsPx.toFixed(1)} px, worst ${cal.worstPx.toFixed(1)} px`
          : 'Tap five targets on the panel; the residual is shown before anything is written.')),
    el('div', { style: 'display:flex;gap:8px;flex-wrap:wrap' },
      el('div', {
        style: 'padding:4px 11px;border-radius:13px;font-size:12px;border:1px solid var(--border2);'
             + 'background:var(--sunken);color:var(--text2)',
      }, 'X11 · xinput'),
      el('div', {
        style: 'padding:4px 11px;border-radius:13px;font-size:12px;border:1px dashed var(--border2);color:var(--text4)',
      }, 'Wayland · not supported')),
    el('div', { style: 'margin-left:auto;display:flex;gap:10px;flex-wrap:wrap' },
      el('button', {
        class: 'btn btn-accent',
        disabled: !state.connected || !isX11 || !state.app.edgePresent,
        title: !state.app.edgePresent ? 'The Edge is not attached to this session' : '',
        onclick: () => startCalibration(),
      }, icon('fa-solid fa-crosshairs', 'margin-right:7px;font-size:12px'), 'Run 5-point calibration'),
      el('button', {
        class: 'btn',
        disabled: !state.connected,
        onclick: async () => {
          try { await api.call('touch.applyOutputMapping'); state.touch = await api.call('touch.state'); }
          catch (err) { touchUi.error = err.message; }
          render();
        },
      }, 'Reset matrix')),
    !isX11
      ? el('div', { style: 'flex-basis:100%' },
          unavailableNote('Touch mapping is X11 only. The transformation matrix, the '
            + 'floating-pointer modes and the raw touch reader all go through xinput and XInput2.'))
      : null);
}

// The whole touch stack is xinput and XInput2, so the honest position is that
// gestures and the lock zone are not built rather than half-working.
function notBuiltCard(kicker, title, body, why) {
  return el('div', { class: 'card', style: 'padding:var(--card-pad);display:flex;flex-direction:column;gap:8px' },
    el('div', { style: 'display:flex;align-items:baseline;gap:10px' },
      el('div', { class: 'card-kicker' }, kicker),
      el('div', { style: 'margin-left:auto;font-size:12.5px;color:var(--text5)' }, 'off')),
    el('div', { class: 'card-title' }, title),
    el('div', { class: 'card-sub' }, body),
    unavailableNote(why));
}

function touchTestCard() {
  const points = [...touchUi.points.values()];
  const streaming = !!state.touch.streaming;
  return el('div', { class: 'card', style: 'padding:var(--card-pad);display:flex;flex-direction:column;gap:8px' },
    el('div', { style: 'display:flex;align-items:baseline;gap:10px' },
      el('div', { class: 'card-kicker' }, 'TOUCH TEST'),
      el('div', { style: 'margin-left:auto;font-size:12px;color:var(--text4)' },
        `${points.length} contact${points.length === 1 ? '' : 's'}`)),
    el('div', {
      style: 'aspect-ratio:2560/720;border-radius:8px;background:var(--sidebar);'
           + 'border:1px solid var(--border);position:relative;max-height:74px;overflow:hidden',
    },
      ...points.map((p) => el('div', {
        style: `position:absolute;left:${(p.nx * 100).toFixed(2)}%;top:${(p.ny * 100).toFixed(2)}%;`
             + 'width:34px;height:34px;margin:-17px 0 0 -17px;border-radius:50%;'
             + 'border:2px solid var(--accent);opacity:.9',
      }))),
    el('div', { class: 'mono', style: 'display:flex;gap:20px;font-size:11.5px;color:var(--text4)' },
      el('div', {}, points.length ? `x ${Math.round(points[0].nx * 2560)}` : 'x —'),
      el('div', {}, points.length ? `y ${Math.round(points[0].ny * 720)}` : 'y —')));
}

// The gestures the recogniser produces, in the order they are worth binding.
const GESTURE_NAMES = [
  'swipe-left', 'swipe-right', 'swipe-up', 'swipe-down',
  'two-finger-swipe-left', 'two-finger-swipe-right',
  'two-finger-tap', 'three-finger-tap',
  'long-press', 'pinch-in', 'pinch-out',
];

const LOCK_SIDES = ['left', 'right', 'top', 'bottom'];

// Shown before "show more". The four single-finger swipes cover most of what
// anyone binds, and listing all eleven made the page taller than the window.
const PRIMARY_GESTURES = 3;

function touchCfg() {
  return state.touchConfig || { gestures: { enabled: false, bindings: {} }, lockZone: {} };
}

async function saveTouchCfg(mutate) {
  const cfg = JSON.parse(JSON.stringify(touchCfg()));
  mutate(cfg);
  try {
    state.touchConfig = await api.call('touch.setConfig', cfg);
    touchUi.error = '';
  } catch (err) {
    touchUi.error = err.message;
  }
  render();
}

// Not yet firing on the panel. The recogniser and its bindings are tested, but
// something between the raw X11 stream and the recogniser is not delivering, so
// the controls are shown disabled rather than pretending to work.
const TOUCH_FEATURES_LIVE = false;

function comingSoon(text) {
  return el('div', { class: 'why' }, icon('fa-solid fa-circle-info'),
    el('span', {}, text));
}

function gesturesCard() {
  const cfg = touchCfg();
  const on = TOUCH_FEATURES_LIVE && !!(cfg.gestures && cfg.gestures.enabled);
  const bindings = (cfg.gestures && cfg.gestures.bindings) || {};
  const actions = cfg.availableActions || { none: 'do nothing' };
  const bound = GESTURE_NAMES.filter((g) => bindings[g] && bindings[g] !== 'none').length;

  return el('div', { class: 'card', style: 'padding:var(--card-pad);display:flex;flex-direction:column;gap:8px;opacity:.55' },
    el('div', { style: 'display:flex;align-items:baseline;gap:10px' },
      el('div', { class: 'card-kicker' }, 'GESTURES'),
      el('div', { style: 'margin-left:auto;font-size:12.5px;color:var(--text4)' },
        `${bound} bound`),
      el('button', {
        class: `toggle${on ? ' on' : ''}`,
        disabled: true,
        onclick: () => {},
      }, el('div', { class: 'knob' }))),

    el('div', { style: `display:flex;flex-direction:column;gap:2px;${on ? '' : 'opacity:.45;'}` },
      ...GESTURE_NAMES.slice(0, PRIMARY_GESTURES).map((g, i, shown) => el('div', {
        style: 'display:flex;justify-content:space-between;align-items:center;gap:10px;padding:3px 0'
             + (i < shown.length - 1 ? ';border-bottom:1px solid var(--border)' : ''),
      },
        el('div', { style: 'font-size:13.5px;color:var(--text2)' }, g.replace(/-/g, ' ')),
        el('select', {
          style: 'background:var(--sunken);border:1px solid var(--border2);border-radius:12px;'
               + 'padding:3px 10px;color:var(--text3);font-family:inherit;font-size:12.5px;outline:none',
          disabled: !on || !state.connected,
          onchange: (e) => saveTouchCfg((c) => {
            c.gestures = c.gestures || {};
            c.gestures.bindings = { ...(c.gestures.bindings || {}) };
            c.gestures.bindings[g] = e.target.value;
          }),
        }, ...Object.entries(actions).map(([id, label]) =>
          el('option', { value: id, selected: (bindings[g] || 'none') === id || null }, label)))))),

    el('div', { style: 'display:flex;align-items:center;gap:12px' },
      el('button', {
        class: 'btn btn-small',
        style: 'border-style:dashed',
        disabled: true,
        onclick: () => {},
      }, `Show ${GESTURE_NAMES.length - PRIMARY_GESTURES} more`),
      // Bindings below the fold would otherwise be invisible.
      (() => {
        const hidden = GESTURE_NAMES.slice(PRIMARY_GESTURES)
          .filter((g) => bindings[g] && bindings[g] !== 'none').length;
        return hidden
          ? el('div', { style: 'font-size:12px;color:var(--text4)' }, `${hidden} more bound`)
          : null;
      })()),

    comingSoon('Not working yet: touches are not reaching the recogniser. Coming later.'));
}

function lockZoneCard() {
  const cfg = touchCfg();
  const z = cfg.lockZone || {};
  const on = TOUCH_FEATURES_LIVE && !!z.enabled;
  const effective = !!cfg.lockZoneEffective;
  const pct = Math.round((z.fraction || 0.4) * 100);
  const side = z.side || 'right';

  // A picture of which band is reserved, at panel proportions.
  const band = el('div', {
    style: 'height:44px;border-radius:7px;background:var(--sunken);position:relative;overflow:hidden',
  },
    on ? el('div', {
      style: `position:absolute;top:0;bottom:0;${
        side === 'left' ? `left:0;width:${pct}%`
        : side === 'right' ? `right:0;width:${pct}%`
        : side === 'top' ? `left:0;right:0;top:0;height:${pct}%`
        : `left:0;right:0;bottom:0;height:${pct}%`};`
           + 'background:repeating-linear-gradient(45deg,var(--border2) 0 6px,transparent 6px 12px);'
           + `border-${side === 'right' ? 'left' : side === 'left' ? 'right' : side === 'top' ? 'bottom' : 'top'}:2px solid var(--accent);`
           + 'display:flex;align-items:center;justify-content:center;font-size:11.5px;color:var(--text2)',
    }, 'ignored')
      : el('div', {
          style: 'position:absolute;inset:0;display:flex;align-items:center;justify-content:center;'
               + 'font-size:11.5px;color:var(--text5)',
        }, 'whole panel accepts touch'));

  return el('div', { class: 'card', style: 'padding:var(--card-pad);display:flex;flex-direction:column;gap:8px;opacity:.55' },
    el('div', { style: 'display:flex;align-items:baseline;gap:10px' },
      el('div', { class: 'card-kicker' }, 'LOCK ZONE'),
      el('div', { style: 'margin-left:auto;font-size:12.5px;color:var(--text4)' }, on ? `${pct}%` : 'off'),
      el('button', {
        class: `toggle${on ? ' on' : ''}`,
        disabled: true,
        onclick: () => {},
      }, el('div', { class: 'knob' }))),

    el('div', { class: 'card-sub' },
      'Ignore touches in a chosen band, so a resting palm cannot fire anything.'),
    band,

    el('div', { style: `display:flex;align-items:center;gap:12px;flex-wrap:wrap;${on ? '' : 'opacity:.45;'}` },
      el('div', { style: 'font-size:13px;color:var(--text3)' }, 'Edge'),
      ...LOCK_SIDES.map((sd) => el('button', {
        class: `chip${side === sd ? ' on' : ''}`,
        style: 'padding:4px 11px;border-radius:12px;font-size:12.5px',
        disabled: !on || !state.connected,
        onclick: () => saveTouchCfg((c) => {
          c.lockZone = { ...(c.lockZone || {}), side: sd, enabled: true,
                         fraction: z.fraction || 0.4 };
        }),
      }, sd)),
      el('input', {
        type: 'range', min: '5', max: '90', value: String(pct),
        style: 'flex:1;min-width:120px',
        disabled: !on || !state.connected,
        onchange: (e) => saveTouchCfg((c) => {
          c.lockZone = { ...(c.lockZone || {}), enabled: true, side,
                         fraction: Number(e.target.value) / 100 };
        }),
      })),

    // The honest limit. Raw X11 touch events can be observed but not cancelled,
    // so a touch can only be thrown away while the agent owns the device, which
    // is what Ripple only mode does by floating it.
    comingSoon('Not working yet. It will only ever apply in Ripple only mode.'));
}

// Every gesture and its binding, in a dialog. The card shows only the few most
// people bind, because listing all eleven made the page taller than the window.
function gesturesModal() {
  const cfg = touchCfg();
  const on = !!(cfg.gestures && cfg.gestures.enabled);
  const bindings = (cfg.gestures && cfg.gestures.bindings) || {};
  const actions = cfg.availableActions || { none: 'do nothing' };

  const row = (g, last) => el('div', {
    style: 'display:grid;grid-template-columns:minmax(0,1fr) 210px;gap:14px;align-items:center;'
         + 'padding:9px 4px' + (last ? '' : ';border-bottom:1px solid var(--border)'),
  },
    el('div', { style: 'font-size:13.5px;color:var(--text2)' }, g.replace(/-/g, ' ')),
    el('select', {
      style: 'background:var(--sunken);border:1px solid var(--border2);border-radius:12px;'
           + 'padding:4px 10px;color:var(--text3);font-family:inherit;font-size:12.5px;outline:none',
      disabled: !on || !state.connected,
      onchange: (e) => saveTouchCfg((c) => {
        c.gestures = c.gestures || {};
        c.gestures.bindings = { ...(c.gestures.bindings || {}) };
        c.gestures.bindings[g] = e.target.value;
      }),
    }, ...Object.entries(actions).map(([id, label]) =>
      el('option', { value: id, selected: (bindings[g] || 'none') === id || null }, label))));

  return el('div', {
    style: 'position:fixed;inset:0;background:rgba(0,0,0,.5);display:flex;align-items:center;'
         + 'justify-content:center;padding:40px;z-index:50',
    onclick: (e) => { if (e.target === e.currentTarget) { touchUi.gesturesOpen = false; render(); } },
  },
    el('div', {
      style: 'width:640px;max-width:100%;max-height:100%;background:var(--bg);'
           + 'border:1px solid var(--shell-border);border-radius:12px;overflow:hidden;'
           + 'display:flex;flex-direction:column;box-shadow:0 30px 80px rgba(0,0,0,.5)',
    },
      el('div', {
        style: 'display:flex;align-items:center;gap:12px;height:48px;background:var(--header);'
             + 'padding:0 18px;flex:none',
      },
        el('div', { style: 'font-size:14px;font-weight:500;color:var(--text)' }, 'All gestures'),
        el('div', { style: 'font-size:12px;color:var(--text4)' },
          on ? 'recognised in every touch mode' : 'gestures are off'),
        el('button', {
          style: 'margin-left:auto;width:24px;height:24px;border-radius:50%;background:var(--border);'
               + 'color:var(--text2);border:none;cursor:pointer',
          onclick: () => { touchUi.gesturesOpen = false; render(); },
        }, '✕')),

      el('div', { style: 'padding:14px 18px;overflow-y:auto' },
        ...GESTURE_NAMES.map((g, i) => row(g, i === GESTURE_NAMES.length - 1))),

      el('div', {
        style: 'padding:12px 18px;background:var(--sidebar);border-top:1px solid var(--border);'
             + 'display:flex;justify-content:flex-end',
      },
        el('button', {
          class: 'btn btn-small',
          onclick: () => { touchUi.gesturesOpen = false; render(); },
        }, 'Done'))));
}

PAGES.touch = (host) => {
  const ids = state.touch.deviceIds || [];
  fill(host,
    pageHead('Touch', ids.length ? `XINPUT ID ${ids.join(' + ')} · CRSR EDGE TOUCH` : 'NO DIGITIZER'),
    touchUi.error
      ? el('div', { class: 'banner warn', style: 'margin:0 0 16px' },
          icon('fa-solid fa-triangle-exclamation'), el('span', {}, touchUi.error))
      : null,
    el('div', { style: 'display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px;margin-bottom:var(--gap-section)' },
      ...TOUCH_MODES.map(modeCard)),
    calibrationCard(),
    el('div', { style: 'height:var(--gap-section)' }),
    el('div', { style: 'display:grid;grid-template-columns:1.2fr 1fr;gap:10px;margin-bottom:var(--gap-section)' },
      gesturesCard(), lockZoneCard()),
    touchTestCard());
};
