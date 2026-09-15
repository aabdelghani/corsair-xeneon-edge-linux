// EdgeLine UI: display layouts findEdgeDisplay has to get right.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Run with: npm test   (plain node, no framework)
'use strict';

const assert = require('assert');
const { findEdgeDisplay } = require('../edge-display');

let nextId = 1;
const d = (width, height) => ({ id: nextId++, size: { width, height } });
const picks = (label, displays, primary, expected) => {
  const got = findEdgeDisplay(displays, primary ? primary.id : undefined);
  assert.strictEqual(got, expected,
    `${label}: expected ${expected ? `${expected.size.width}x${expected.size.height}` : 'none'}, `
    + `got ${got ? `${got.size.width}x${got.size.height}` : 'none'}`);
  console.log(`  ok  ${label}`);
};

// Native, no scaling: exact match, the case that has always worked.
{ const main = d(3440, 1440); const edge = d(2560, 720);
  picks('native Edge beside an ultrawide', [main, edge], main, edge); }
{ const edge = d(2560, 720);
  picks('Edge as the only, primary display', [edge], edge, edge); }

// PR #3's case: XWayland with a fractionally scaled primary.
{ const main = d(3840, 1440); const edge = d(2555, 719);
  picks('XWayland 145%, Electron DIP 2555x719', [main, edge], main, edge); }
{ const main = d(5568, 2088); const edge = d(3712, 1044);
  picks('XWayland 145%, raw X11 3712x1044', [main, edge], main, edge); }

// The regression a plain aspect-ratio first-match would introduce.
{ const wide = d(5120, 1440);
  picks('49in 32:9 as the only display is not the Edge', [wide], wide, null); }
{ const wide = d(5120, 1440); const edge = d(2560, 720);
  picks('49in 32:9 primary listed first, real Edge present', [wide, edge], wide, edge); }
{ const wide = d(5120, 1440); const edge = d(2555, 719);
  picks('49in 32:9 primary, scaled Edge present', [wide, edge], wide, edge); }
{ const main = d(3440, 1440); const wide = d(5120, 1440);
  picks('49in 32:9 as a secondary, no Edge', [main, wide], main, null); }
{ const wide = d(3840, 1080); const other = d(1920, 1080);
  picks('3840x1080 32:9 primary, no Edge', [wide, other], wide, null); }

// Other strips and small displays.
{ const main = d(3440, 1440); const strip = d(3840, 1100);
  picks('Wisecoco 3840x1100 strip is not 32:9', [main, strip], main, null); }
{ const main = d(1920, 1080); const small = d(1920, 540);
  picks('small 32:9 display below the size floor', [main, small], main, null); }

// More than one candidate: closest to 2560 wide wins.
{ const main = d(3840, 1440); const a = d(3712, 1044); const b = d(2555, 719);
  picks('two ratio candidates, the nearer to 2560 wins', [main, a, b], main, b); }

picks('no displays at all', [], null, null);
console.log('edge-display: all cases pass');
