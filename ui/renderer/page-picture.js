// Edgeline: the Picture page.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every control here is built from what the panel reports, not from the
// mockup. The mockup drew a sharpness range of 0..10, four colour presets and a
// USB-C/HDMI input pair; this panel's maximum sharpness is 4, it offers seven
// presets, and its input list contains no USB-C at all. Anything the panel does
// not advertise is rendered disabled with the reason attached rather than
// hidden, so the window stays a truthful picture of the hardware.
'use strict';

const VCP = {
  BRIGHTNESS: 0x10, CONTRAST: 0x12, PRESET: 0x14,
  GAIN_R: 0x16, GAIN_G: 0x18, GAIN_B: 0x1a,
  SHARPNESS: 0x87, INPUT: 0x60, POWER: 0xd6,
};

// The preset that unlocks per-channel gain. On this panel that is "User 1";
// the mockup called the same idea "Custom".
const USER_PRESET_LABEL = /^user/i;

// Writes are debounced so a slider drag issues one setvcp on release rather
// than one per pixel. The agent also coalesces per code, so this is belt and
// braces, but it keeps the i2c bus quiet during a drag.
const WRITE_DEBOUNCE_MS = 250;
const pendingWrites = new Map();

function writeVcp(code, value) {
  clearTimeout(pendingWrites.get(code));
  pendingWrites.set(code, setTimeout(async () => {
    pendingWrites.delete(code);
    try {
      await api.call('ddc.set', { code, value });
    } catch (err) {
      state.ddc.log = [...(state.ddc.log || []), `set ${code.toString(16)}: ${err.message}`].slice(-40);
      render();
    }
  }, WRITE_DEBOUNCE_MS));
}

/** Slider row: label, range, live value. Disabled when the panel lacks it. */
function sliderRow(label, code, opts = {}) {
  const v = vcp(code);
  const f = feature(code);
  const supported = !!f || !!v;
  const max = v && v.max > 0 ? v.max : (opts.fallbackMax || 100);
  const value = v ? v.value : 0;

  const out = el('div', {
    class: 'mono',
    style: 'font-size:13px;color:var(--text3);text-align:right',
  }, supported ? String(value) : '—');

  const input = el('input', {
    type: 'range', min: 0, max: String(max), value: String(value),
    style: 'width:100%',
    disabled: !supported || !state.ddc.ready,
    oninput: (e) => { out.textContent = e.target.value; },
    onchange: (e) => writeVcp(code, Number(e.target.value)),
  });

  return el('div', { style: 'display:grid;grid-template-columns:minmax(70px,96px) minmax(0,1fr) 40px;align-items:center;gap:16px' },
    el('div', { style: 'font-size:14px;color:var(--text2)' }, label),
    input,
    out);
}

function slidersCard() {
  const sharp = feature(VCP.SHARPNESS);
  return el('div', { class: 'card', style: 'padding:22px;display:flex;flex-direction:column;gap:20px' },
    sliderRow('Brightness', VCP.BRIGHTNESS),
    sliderRow('Contrast', VCP.CONTRAST),
    sharp || vcp(VCP.SHARPNESS)
      ? sliderRow('Sharpness', VCP.SHARPNESS, { fallbackMax: 4 })
      : el('div', {},
          sliderRow('Sharpness', VCP.SHARPNESS),
          unavailableNote('This panel does not advertise sharpness (VCP 0x87).')));
}

function presetRow() {
  const f = feature(VCP.PRESET);
  const cur = vcp(VCP.PRESET);
  if (!f || !f.values.length) {
    return el('div', { style: 'display:flex;flex-direction:column;gap:6px' },
      el('div', { style: 'display:flex;align-items:center;gap:14px' },
        el('div', { style: 'font-size:14px;color:var(--text2);width:96px;flex:none' }, 'Preset'),
        el('div', { class: 'card-sub' }, 'not available')),
      unavailableNote('This panel does not advertise colour presets (VCP 0x14).'));
  }
  return el('div', { style: 'display:flex;align-items:center;gap:14px;flex-wrap:wrap' },
    el('div', { style: 'font-size:14px;color:var(--text2);width:96px;flex:none' }, 'Preset'),
    el('div', { style: 'display:flex;gap:8px;flex-wrap:wrap' },
      ...f.values.map((v) => el('button', {
        class: `chip${cur && cur.value === v.code ? ' on' : ''}`,
        disabled: !state.ddc.ready,
        onclick: () => writeVcp(VCP.PRESET, v.code),
      }, v.label))));
}

function gainCard() {
  const f = feature(VCP.PRESET);
  const cur = vcp(VCP.PRESET);
  const curLabel = f && cur ? (f.values.find((v) => v.code === cur.value) || {}).label || '' : '';
  // Gain is only writable under the user preset; every other preset drives the
  // channels itself and a write would be silently overridden.
  // A measured ICC profile carries its own calibration curves; letting gain be
  // dragged underneath it would silently invalidate them.
  const iccLocked = !!(state.color && state.color.calibrated);
  const unlocked = USER_PRESET_LABEL.test(curLabel) && !iccLocked;
  const userPreset = f ? f.values.find((v) => USER_PRESET_LABEL.test(v.label)) : null;

  const channels = [
    ['R', VCP.GAIN_R, '#c7162b'],
    ['G', VCP.GAIN_G, '#0e8420'],
    ['B', VCP.GAIN_B, '#2e7cbb'],
  ];

  return el('div', {
    class: 'card',
    style: `padding:22px;display:flex;flex-direction:column;gap:16px;opacity:${unlocked ? 1 : 0.45}`,
  },
    el('div', { style: 'display:flex;justify-content:space-between;align-items:baseline' },
      el('div', { style: 'font-size:14px;color:var(--text2)' }, 'RGB gain'),
      el('div', { style: 'font-size:12px;color:var(--text4)' },
        unlocked ? 'editable'
          : iccLocked ? 'locked by the bound ICC profile'
          : `enabled by the ${userPreset ? userPreset.label : 'user'} preset`)),
    ...channels.map(([name, code, colour]) => {
      const v = vcp(code);
      const max = v && v.max > 0 ? v.max : 255;
      const value = v ? v.value : 0;
      const pct = max ? Math.round((value / max) * 100) : 0;
      const out = el('div', { class: 'mono', style: 'font-size:13px;color:var(--text3);text-align:right' },
        v ? String(value) : '—');
      return el('div', { style: 'display:grid;grid-template-columns:18px 1fr 40px;align-items:center;gap:14px' },
        el('div', { class: 'mono', style: `font-size:13px;color:${colour}` }, name),
        unlocked
          ? el('input', {
              type: 'range', min: 0, max: String(max), value: String(value),
              style: 'width:100%',
              disabled: !state.ddc.ready,
              oninput: (e) => { out.textContent = e.target.value; },
              onchange: (e) => writeVcp(code, Number(e.target.value)),
            })
          : el('div', { style: 'height:6px;border-radius:3px;background:var(--border2)' },
              el('div', { style: `width:${pct}%;height:6px;border-radius:3px;background:${colour}` })),
        out);
    }));
}

function inputSourceRow() {
  const f = feature(VCP.INPUT);
  const cur = vcp(VCP.INPUT);
  const body = [
    el('div', { style: 'font-size:14px;color:var(--text2)' }, 'Input source'),
    el('div', { class: 'mono', style: 'font-size:11.5px;color:var(--text4)' }, 'VCP 60'),
  ];
  if (f && f.values.length) {
    body.push(el('div', { style: 'margin-left:auto;display:flex;gap:8px;flex-wrap:wrap' },
      ...f.values.map((v) => el('button', {
        class: `chip${cur && cur.value === v.code ? ' on' : ''}`,
        style: 'padding:5px 13px;border-radius:14px;font-size:12.5px',
        disabled: !state.ddc.ready,
        onclick: () => writeVcp(VCP.INPUT, v.code),
      }, v.label))));
  } else {
    body.push(el('div', { style: 'margin-left:auto', class: 'card-sub' }, 'not available'));
  }
  return el('div', { class: 'card', style: 'padding:14px 18px;display:flex;align-items:center;gap:14px;flex-wrap:wrap' },
    ...body,
    // The panel lists inputs it plainly does not have, so say so rather than
    // letting someone switch the display to a dead DVI socket and wonder why.
    f && f.values.length
      ? el('div', { style: 'flex-basis:100%' },
          unavailableNote('The scaler advertises a generic input list. Switching to a socket this panel does not have will blank it until you switch back.'))
      : null);
}

function actionRow() {
  const power = vcp(VCP.POWER);
  const blanked = power ? power.value !== 1 : false;
  const canBlank = !!feature(VCP.POWER) || !!power;
  const canReset = !!feature(0x04) || !!feature(0x05) || !!feature(0x08);

  return el('div', { style: 'display:flex;gap:10px;align-items:center;flex-wrap:wrap' },
    el('button', {
      class: `btn${blanked ? ' btn-accent' : ''}`,
      disabled: !canBlank || !state.ddc.ready,
      onclick: () => writeVcp(VCP.POWER, blanked ? 0x01 : 0x05),
    }, icon('fa-regular fa-eye-slash', 'margin-right:7px;font-size:12px'),
       blanked ? 'Unblank panel' : 'Blank panel'),
    el('button', {
      class: 'btn',
      disabled: !canReset || !state.ddc.ready,
      onclick: () => api.call('ddc.restoreDefaults', { scope: 'factory' }).catch(() => {}),
    }, icon('fa-solid fa-rotate-left', 'margin-right:7px;font-size:12px'), 'Reset to defaults'),
    el('div', { style: 'margin-left:auto;font-size:12px;color:var(--text5)' },
      'writes debounced on release'));
}

// ---------------------------------------------------------------- right rail

function deviceCard() {
  const edge = state.app.edgePresent;
  return el('div', { class: 'card', style: 'padding:16px;display:flex;flex-direction:column;gap:12px' },
    el('div', { class: 'card-kicker' }, 'DEVICE'),
    el('div', { style: 'border-radius:8px;background:radial-gradient(120% 100% at 50% 40%,var(--sel) 0%,var(--card) 70%);padding:6px 4px' },
      el('img', { src: '../assets/img/xeneon-edge.png', alt: 'Corsair Xeneon Edge',
                  style: 'width:100%;display:block' })),
    el('div', { style: 'display:flex;flex-direction:column;gap:5px' },
      el('div', { class: 'card-title' }, 'Corsair Xeneon Edge'),
      el('div', { style: 'font-size:12px;color:var(--text4)' },
        edge ? '2560 × 720 · 14.5″ · attached' : '2560 × 720 · 14.5″ · not attached')));
}

function ddcLogCard() {
  const lines = (state.ddc.log || []).slice(-6);
  return el('div', { class: 'card', style: 'padding:16px;display:flex;flex-direction:column;gap:10px' },
    el('div', { class: 'card-kicker' }, 'DDC LOG'),
    el('div', { class: 'mono', style: 'font-size:11.5px;line-height:1.9;color:var(--text3);word-break:break-all' },
      ...(lines.length
        ? lines.map((l) => el('div', {
            style: /fail|error|unparsed|refus/i.test(l) ? 'color:#f99b11' : '',
          }, l))
        : [el('div', { style: 'color:var(--text5)' }, 'nothing yet')])));
}

// Ambient and per-app rules are on this page in the design but belong to a
// later phase. They are rendered in place and marked, rather than left out, so
// the page matches the design's shape and nobody wonders where they went.
function notYetCard(kicker, title, body) {
  return el('div', { class: 'card', style: 'padding:16px;display:flex;flex-direction:column;gap:9px' },
    el('div', { class: 'card-kicker' }, kicker),
    el('div', { class: 'card-title' }, title),
    el('div', { class: 'card-sub' }, body),
    unavailableNote('Not wired up in this build.'));
}

function rulesCard() {
  const r = state.rules || {};
  const count = (r.rules || []).length;
  const focused = r.focused || {};
  const summary = count
    ? (r.rules.slice(0, 2)
        .map((x) => `${x.app || x.pattern} → ${x.profile}`)
        .join(' · ') + (count > 2 ? ` · +${count - 2} more` : ''))
    : 'No rules yet.';

  return el('div', { class: 'card', style: 'padding:16px;display:flex;flex-direction:column;gap:9px' },
    el('div', { style: 'display:flex;align-items:baseline;gap:10px' },
      el('div', { class: 'card-title' }, 'Per-app rules'),
      el('div', {
        style: 'margin-left:auto;font-size:11.5px;padding:2px 9px;border-radius:11px;'
             + (r.enabled ? 'background:var(--accent);color:var(--on-accent)'
                          : 'border:1px solid var(--border2);color:var(--text3)'),
      }, r.enabled ? 'on' : 'off')),
    el('div', { class: 'card-sub' }, summary),
    focused.valid
      ? el('div', { class: 'mono', style: 'font-size:11px;color:var(--text5)' },
          `focused: ${focused.wmClassFull}`)
      : null,
    el('div', {
      style: `font-size:12.5px;color:var(--accent);cursor:${state.connected ? 'pointer' : 'default'}`,
      onclick: () => { if (state.connected) openRulesModal(); },
    }, 'Edit rules ›'));
}

function iccCard() {
  const c = state.color || {};
  if (!c.available) {
    return el('div', { class: 'card', style: 'padding:16px;display:flex;flex-direction:column;gap:9px' },
      el('div', { style: 'display:flex;justify-content:space-between;align-items:baseline' },
        el('div', { class: 'card-title' }, 'ICC profile'),
        el('div', { style: 'font-size:12.5px;color:var(--text4)' }, 'colord')),
      unavailableNote(c.reason || 'colord is not available.'));
  }
  const name = (c.defaultProfile || '').split('/').pop() || 'none';
  return el('div', { class: 'card', style: 'padding:16px;display:flex;flex-direction:column;gap:9px' },
    el('div', { style: 'display:flex;justify-content:space-between;align-items:baseline;gap:10px' },
      el('div', { class: 'card-title' }, 'ICC profile'),
      el('div', { style: 'font-size:12.5px;color:var(--text4)' }, 'colord')),
    el('div', { class: 'mono', style: 'font-size:11.5px;color:var(--text3);word-break:break-all' }, name),
    el('div', { style: 'font-size:12px;color:var(--text4)' },
      c.calibrated
        ? 'A measured profile is bound, so gain is locked to protect it.'
        // colord gives every display an automatic EDID profile. Calling that a
        // calibration would lock the gain sliders on a panel nobody measured.
        : 'Automatic EDID profile, not a calibration. Gain stays editable.'));
}

PAGES.picture = (host) => {
  const left = el('div', { style: 'min-width:0;display:flex;flex-direction:column;gap:24px' },
    slidersCard(),
    presetRow(),
    gainCard(),
    el('div', { style: 'display:grid;grid-template-columns:1fr 1fr;gap:12px' },
      rulesCard(), iccCard()),
    inputSourceRow(),
    actionRow());

  const right = el('div', { style: 'display:flex;flex-direction:column;gap:16px' },
    deviceCard(),
    ddcLogCard(),
    notYetCard('AMBIENT', 'Auto brightness', 'Follow a sunset schedule, or a webcam lux reading.'));

  fill(host,
    pageHead('Picture', 'DDC/CI'),
    !state.ddc.ready
      ? el('div', { class: 'banner warn', style: 'margin:0 0 20px' },
          icon('fa-solid fa-triangle-exclamation'),
          el('span', {}, state.connected
            ? (state.ddc.message || 'looking for the panel on DDC…')
            : 'The agent is not running, so nothing here can reach the panel.'))
      : null,
    el('div', { style: 'display:grid;grid-template-columns:minmax(0,1fr) 330px;gap:26px;align-items:start' },
      left, right));
};
