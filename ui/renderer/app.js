// Edgeline renderer.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Vanilla DOM, no framework and no bundler, matching the convention already
// working in this codebase's sibling projects. The renderer holds no hardware
// state of its own: it takes a snapshot from the agent on connect and then
// follows events, so a reconnect repaints correctly with no special casing.
'use strict';

const api = window.edgeline;

// ---------------------------------------------------------------- helpers

/** Build an element. Children may be nodes or strings. */
function el(tag, attrs = {}, ...children) {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v === null || v === undefined || v === false) continue;
    if (k === 'class') node.className = v;
    else if (k === 'style') node.style.cssText = v;
    else if (k === 'html') node.innerHTML = v;
    else if (k.startsWith('on') && typeof v === 'function') node.addEventListener(k.slice(2), v);
    else if (k === 'dataset') Object.assign(node.dataset, v);
    else node.setAttribute(k, v);
  }
  for (const c of children.flat()) {
    if (c === null || c === undefined || c === false) continue;
    node.append(c.nodeType ? c : document.createTextNode(String(c)));
  }
  return node;
}

const icon = (cls, style) => el('i', { class: cls, style });
const $ = (sel) => document.querySelector(sel);

/** Replace a container's children in one go. */
function fill(node, ...children) {
  node.replaceChildren(...children.flat().filter(Boolean));
  return node;
}

// ---------------------------------------------------------------- state

const state = {
  tab: 'picture',
  theme: 'ubuntu-dark',
  connected: false,
  system: {},
  device: {},
  ddc: { ready: false, values: {}, features: [], log: [] },
  touch: {},
  sensors: {},
  update: { state: 'idle' },
  rules: { enabled: false, rules: [], focused: {} },
  color: { available: false },
  app: {},
};

const listeners = new Set();
function onState(fn) { listeners.add(fn); }
function emit() { for (const fn of listeners) fn(); }

/** Latest cached value for a VCP code, or null when the agent has none yet. */
function vcp(code) {
  const key = code.toString(16).padStart(2, '0');
  const v = state.ddc.values[key];
  return v && v.value >= 0 ? v : null;
}

/** The panel's own description of a feature, or null when unsupported. */
function feature(code) {
  return state.ddc.features.find((f) => f.code === code) || null;
}

// ---------------------------------------------------------------- pages

const PAGES = {};   // filled in by page modules below

function render() {
  for (const [name, page] of Object.entries(PAGES)) {
    const host = document.getElementById(`page-${name}`);
    if (!host) continue;
    const active = name === state.tab;
    host.classList.toggle('active', active);
    if (active) page(host);
  }
  renderChrome();

  // Modals live outside the page host so switching tabs behind one is not
  // possible and so they are not rebuilt by a page re-render.
  let layer = document.getElementById('modal-layer');
  if (!layer) {
    layer = el('div', { id: 'modal-layer' });
    document.body.append(layer);
  }
  const modal = typeof rulesModal === 'function' && rulesEditor.open ? rulesModal() : null;
  layer.replaceChildren(...(modal ? [modal] : []));
}

function renderChrome() {
  const st = $('#tbStatus');
  const txt = $('#tbStatusText');
  if (state.connected && state.ddc.ready) {
    st.classList.remove('down');
    const bus = (state.ddc.message || '').match(/bus (\d+)/);
    txt.textContent = `Xeneon Edge · i2c-${bus ? bus[1] : '?'}`;
  } else if (state.connected) {
    st.classList.add('down');
    txt.textContent = state.device.present ? 'panel found, no DDC yet' : 'no panel';
  } else {
    st.classList.add('down');
    txt.textContent = 'agent not running';
  }

  const pill = $('#profilePill');
  if (pill) {
    const name = (typeof profileState !== 'undefined' && profileState.active) || '';
    pill.textContent = name ? `Profile: ${name} \u25be` : 'Profile: none \u25be';
    pill.onclick = () => setTab('profiles');
  }

  $('#footDdcutil').textContent = `ddcutil ${state.system.ddcutil || '—'}`;
  $('#footXinput').textContent = `xinput ${state.system.xinput || '—'}`;
  const det = $('#footDetect');
  det.textContent = state.ddc.ready ? 'detect ok' : (state.connected ? 'no panel' : 'no agent');
  det.className = state.ddc.ready ? 'ok' : 'bad';
}

// ---------------------------------------------------------------- shell

function setTab(tab) {
  state.tab = tab;
  for (const b of document.querySelectorAll('.nav-item'))
    b.classList.toggle('active', b.dataset.tab === tab);
  render();
}

const THEMES = [
  { id: 'ubuntu-dark',  label: 'Ubuntu dark' },
  { id: 'ubuntu-light', label: 'Ubuntu light' },
  { id: 'fedora-dark',  label: 'Fedora dark' },
  { id: 'fedora-light', label: 'Fedora light' },
];

function setTheme(theme) {
  if (!THEMES.some((t) => t.id === theme)) return;
  state.theme = theme;
  document.body.dataset.theme = theme;
  try { localStorage.setItem('theme', theme); } catch { /* private mode */ }
  render();
}

/** First run follows the desktop's light/dark preference rather than guessing. */
function initialTheme(prefersDark) {
  try {
    const saved = localStorage.getItem('theme');
    if (saved && THEMES.some((t) => t.id === saved)) return saved;
  } catch { /* fall through */ }
  return prefersDark ? 'ubuntu-dark' : 'ubuntu-light';
}

function wireShell() {
  for (const b of document.querySelectorAll('.nav-item'))
    b.addEventListener('click', () => setTab(b.dataset.tab));
  for (const b of document.querySelectorAll('[data-win]'))
    b.addEventListener('click', () => api.windowAction(b.dataset.win));
}

// ---------------------------------------------------------------- agent

async function refreshAll() {
  try {
    const all = await api.call('state.all');
    Object.assign(state, {
      system: all.system || {},
      device: all.device || {},
      ddc: all.ddc || state.ddc,
      touch: all.touch || {},
      sensors: all.sensors || {},
      rules: all.rules || state.rules,
      color: all.color || state.color,
    });
  } catch {
    // Leave the last known state on screen rather than blanking the UI.
  }
  if (typeof loadProfiles === 'function') await loadProfiles();
  emit();
  render();
}

function wireAgent() {
  api.onStatus((st) => {
    state.connected = st.connected;
    if (st.connected) refreshAll();
    else { emit(); render(); }
  });

  api.onEvent((msg) => {
    const d = msg.data || {};
    switch (msg.event) {
      case 'ddc': state.ddc = d; break;
      case 'ddcLog':
        state.ddc.log = [...(state.ddc.log || []), d.line].slice(-40);
        break;
      case 'device': state.device = d; break;
      case 'touch': state.touch = d; break;
      case 'sensors': state.sensors = d; break;
      case 'rules': state.rules = d; break;
      case 'focus':
        state.rules = { ...state.rules, focused: d };
        break;
      case 'update': state.update = d; break;
      case 'profiles':
        if (typeof loadProfiles === 'function') loadProfiles();
        return;
      case 'touch.point': /* handled by the ripple overlay, not this window */ return;
      default: return;
    }
    emit();
    render();
  });
}

// ---------------------------------------------------------------- boot

async function boot() {
  wireShell();
  wireAgent();

  state.app = await api.appInfo();
  setTheme(state.app.startTheme || initialTheme(state.app.prefersDark));
  if (state.app.startTab) state.tab = state.app.startTab;
  const st = await api.connected();
  state.connected = st.connected;
  if (st.connected) await refreshAll();
  setTab(state.tab);
}

document.addEventListener('DOMContentLoaded', boot);
