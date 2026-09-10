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

const touchUi = { busy: false, error: '', points: new Map(), lastCal: null };

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
    style: 'border-radius:12px;padding:18px;cursor:pointer;background:var(--card);'
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
    el('div', {
      style: 'height:52px;border:1px dashed var(--border2);border-radius:7px;margin-top:12px;'
           + 'display:flex;align-items:center;justify-content:center;font-size:13px;color:var(--text4)',
    }, m.glyph),
    el('div', { style: 'font-size:13px;line-height:1.5;color:var(--text3);margin-top:12px' }, m.desc));
}

function calibrationCard() {
  const cal = touchUi.lastCal;
  const isX11 = (state.system.sessionType || '').toLowerCase() === 'x11';
  return el('div', { class: 'card', style: 'padding:20px;display:flex;align-items:center;gap:20px;flex-wrap:wrap' },
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
  return el('div', { class: 'card', style: 'padding:18px;display:flex;flex-direction:column;gap:12px' },
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
  return el('div', { class: 'card', style: 'padding:20px;display:flex;flex-direction:column;gap:12px' },
    el('div', { style: 'display:flex;align-items:baseline;gap:10px' },
      el('div', { class: 'card-kicker' }, 'TOUCH TEST'),
      el('div', { style: 'margin-left:auto;font-size:12px;color:var(--text4)' },
        streaming ? `${points.length} contact${points.length === 1 ? '' : 's'}`
                  : 'select Ripple only to stream touches')),
    el('div', {
      style: 'aspect-ratio:2560/720;border-radius:8px;background:var(--sidebar);'
           + 'border:1px solid var(--border);position:relative;max-height:180px;overflow:hidden',
    },
      ...points.map((p) => el('div', {
        style: `position:absolute;left:${(p.nx * 100).toFixed(2)}%;top:${(p.ny * 100).toFixed(2)}%;`
             + 'width:34px;height:34px;margin:-17px 0 0 -17px;border-radius:50%;'
             + 'border:2px solid var(--accent);opacity:.9',
      }))),
    el('div', { class: 'mono', style: 'display:flex;gap:24px;font-size:12px;color:var(--text4);flex-wrap:wrap' },
      el('div', {}, points.length ? `x ${Math.round(points[0].nx * 2560)}` : 'x —'),
      el('div', {}, points.length ? `y ${Math.round(points[0].ny * 720)}` : 'y —'),
      el('div', {}, `contacts ${points.length}`),
      el('div', {}, 'pressure n/a')));
}

PAGES.touch = (host) => {
  const ids = state.touch.deviceIds || [];
  fill(host,
    pageHead('Touch', ids.length ? `XINPUT ID ${ids.join(' + ')} · CRSR EDGE TOUCH` : 'NO DIGITIZER'),
    touchUi.error
      ? el('div', { class: 'banner warn', style: 'margin:0 0 16px' },
          icon('fa-solid fa-triangle-exclamation'), el('span', {}, touchUi.error))
      : null,
    el('div', { style: 'display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:14px;margin-bottom:22px' },
      ...TOUCH_MODES.map(modeCard)),
    calibrationCard(),
    el('div', { style: 'height:14px' }),
    el('div', { style: 'display:grid;grid-template-columns:1.2fr 1fr;gap:14px;margin-bottom:14px' },
      notBuiltCard('GESTURES', 'Swipes and pinches',
        'Swipe to change profile or dashboard page, two-finger tap to blank, pinch to resize tiles.',
        'Not implemented. The raw touch stream that would drive it only reaches this app in '
        + 'Ripple only mode, where the digitizer drives no pointer.'),
      notBuiltCard('LOCK ZONE', 'Ignore part of the panel',
        'Reserve a band so a resting palm cannot fire anything mid-game.',
        'Not implemented. Dropping a touch before anything sees it is only possible in '
        + 'Ripple only mode; in the pointer modes X delivers it directly.')),
    touchTestCard());
};
