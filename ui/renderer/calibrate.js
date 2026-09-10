// EdgeLine: the calibration takeover, shown on the panel itself.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Collects five taps and hands them to the agent, which owns the maths. The
// solve is shown before it is written: the old Qt app applied the matrix the
// moment it solved, so a bad run silently became your new calibration.
'use strict';

const api = window.edgeline;

// Fractions of the panel, matching the targets the old overlay used.
const TARGETS = [
  [0.12, 0.18], [0.88, 0.18], [0.50, 0.50], [0.12, 0.82], [0.88, 0.82],
];

const measured = [];
let index = 0;
let baseMatrix = null;
let done = false;

const wrap = document.getElementById('wrap');
const prompt = document.getElementById('prompt');
const pips = document.getElementById('pips');
const result = document.getElementById('result');

function layout() {
  for (const node of wrap.querySelectorAll('.target')) node.remove();
  TARGETS.forEach(([fx, fy], i) => {
    const t = document.createElement('div');
    t.className = 'target' + (i < index ? ' done' : i === index ? ' active' : '');
    t.style.left = `${fx * 100}%`;
    t.style.top = `${fy * 100}%`;
    t.append(Object.assign(document.createElement('div'), { className: 'dot' }));
    wrap.append(t);
  });
  pips.replaceChildren(...TARGETS.map((_, i) => {
    const p = document.createElement('div');
    p.className = 'pip' + (i < index ? ' on' : '');
    return p;
  }));
  prompt.textContent = done
    ? ''
    : `Touch target ${index + 1} of ${TARGETS.length}. Esc cancels.`;
}

function record(x, y) {
  if (done || index >= TARGETS.length) return;
  measured.push({ x, y });
  index += 1;
  layout();
  if (index === TARGETS.length) solve();
}

async function solve() {
  done = true;
  prompt.textContent = '';
  const rect = { w: window.innerWidth, h: window.innerHeight };
  const disp = await api.edgeDisplay();

  // The matrix maps into the whole X screen, not this one monitor, so the
  // agent needs the virtual desktop bounds and where the panel sits in them.
  const params = {
    targets: TARGETS.map(([fx, fy]) => ({ x: fx * rect.w, y: fy * rect.h })),
    measured,
    virtualWidth: window.screen.width,
    virtualHeight: window.screen.height,
    originX: disp ? disp.bounds.x : 0,
    originY: disp ? disp.bounds.y : 0,
    base: baseMatrix || undefined,
    apply: false,
  };

  try {
    const r = await api.call('touch.calibrate', params);
    showResult(r);
  } catch (err) {
    showError(err.message);
  }
}

function button(label, accent, onClick) {
  const b = document.createElement('button');
  b.className = 'btn' + (accent ? ' btn-accent' : '');
  b.textContent = label;
  b.addEventListener('click', onClick);
  return b;
}

function showResult(r) {
  document.getElementById('resultTitle').textContent =
    `Residual ${r.rmsPx.toFixed(1)} px`;
  document.getElementById('resultDetail').textContent =
    `worst point ${r.worstPx.toFixed(1)} px. Nothing has been written yet.`;
  document.getElementById('resultButtons').replaceChildren(
    button('Apply', true, async () => {
      try {
        await api.call('touch.setMatrix', { matrix: r.matrix });
        api.closeCalibration();
      } catch (err) { showError(err.message); }
    }),
    button('Redo', false, () => {
      measured.length = 0;
      index = 0;
      done = false;
      result.classList.remove('show');
      layout();
    }),
    button('Cancel', false, () => api.closeCalibration()));
  result.classList.add('show');
}

function showError(msg) {
  document.getElementById('resultTitle').textContent = 'Calibration failed';
  document.getElementById('resultDetail').textContent = msg;
  document.getElementById('resultButtons').replaceChildren(
    button('Redo', true, () => {
      measured.length = 0;
      index = 0;
      done = false;
      result.classList.remove('show');
      layout();
    }),
    button('Cancel', false, () => api.closeCalibration()));
  result.classList.add('show');
}

// Touch arrives as a pointer event here, because in every mode except Ripple
// the digitizer drives a pointer. That is also what makes the measurement
// meaningful: it records where the pointer LANDED, which is what the matrix
// has to correct.
window.addEventListener('pointerdown', (e) => record(e.clientX, e.clientY));
window.addEventListener('keydown', (e) => { if (e.key === 'Escape') api.closeCalibration(); });

(async () => {
  try {
    const st = await api.call('touch.state');
    baseMatrix = st.matrix && st.matrix.length === 9 ? st.matrix : null;
  } catch { /* the agent will fall back to reading it itself */ }
  layout();
})();
