// EdgeLine: touch feedback for Ripple only mode.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// In Ripple mode the digitizer is floated and drives no pointer, so nothing on
// screen reacts to a touch. This window is the reaction. It is click-through,
// so it never becomes something you can accidentally hit.
'use strict';

const api = window.edgeline;
const live = new Map();

function place(node, nx, ny) {
  node.style.left = `${nx * window.innerWidth}px`;
  node.style.top = `${ny * window.innerHeight}px`;
}

function begin(id, nx, ny) {
  const node = document.createElement('div');
  node.className = 'ripple';
  node.style.width = '10px';
  node.style.height = '10px';
  node.style.opacity = '1';
  place(node, nx, ny);
  document.body.append(node);
  live.set(id, node);
  // Grow and fade on the compositor rather than in script.
  requestAnimationFrame(() => {
    node.style.transition = 'width .22s ease-out, height .22s ease-out';
    node.style.width = '64px';
    node.style.height = '64px';
  });
}

function end(id) {
  const node = live.get(id);
  if (!node) return;
  live.delete(id);
  node.style.transition = 'opacity .28s ease-out, width .28s ease-out, height .28s ease-out';
  node.style.opacity = '0';
  node.style.width = '110px';
  node.style.height = '110px';
  setTimeout(() => node.remove(), 320);
}

api.onEvent((msg) => {
  if (msg.event !== 'touch.point') return;
  const d = msg.data;
  if (d.phase === 'begin') begin(d.id, d.nx, d.ny);
  else if (d.phase === 'update') {
    const node = live.get(d.id);
    if (node) place(node, d.nx, d.ny);
  } else end(d.id);
});

// A stream that stopped would leave stale circles on the panel forever.
api.onStatus((st) => {
  if (st.connected) return;
  for (const [, node] of live) node.remove();
  live.clear();
});
