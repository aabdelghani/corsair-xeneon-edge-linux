// EdgeLine UI: which of Electron's displays is the Xeneon Edge.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pure, so it can be tested with a table of display layouts instead of a desk
// full of monitors. main.js hands it screen.getAllDisplays() and the primary
// display's id.
//
// Found by resolution, never by a remembered connector name: the connector
// index changes across reboots and recables on this hardware.
//
// Exact 2560x720 is tried first, and an exact match is always trusted.
//
// Exact matching alone breaks under XWayland (reported and fixed by mmomega in
// PR #3). X11 has no concept of per-output scale, so when any other monitor in
// the session uses fractional scaling, XWayland picks one global factor and
// reports every output's geometry pre-multiplied by it: a 145% primary turns
// the Edge's real 2560x720 into 3712x1044 at the X11 level. Electron converts
// that back to its own DIP size with its own detected factor, which rounds a
// little differently (observed 2555x719 at 145%). The 32:9 aspect ratio
// survives that uniform scaling, so it is the fallback.
//
// Aspect ratio alone is not enough, though, because the Edge is not the only
// 32:9 display. A 49 inch super-ultrawide at 5120x1440 or 3840x1080 is exactly
// 32:9 as well, and with a plain first-match one would be taken for the Edge,
// with the always-on-top dashboard opened across somebody's main monitor. So
// the ratio fallback:
//   - never picks the primary display, which is where a 49 inch lives and
//     where the Edge practically never is (the exact match still covers a
//     session whose only or primary display really is the Edge);
//   - ignores anything wider than 4000, which keeps the XWayland raw size of
//     the Edge (3712x1044 at 145%) and drops a 5120x1440 monitor;
//   - prefers the candidate closest to 2560 wide when there is more than one.
//
// What it still cannot tell apart: a 3840x1080 super-ultrawide set as a
// secondary display while no Edge is connected. Resolution cannot separate
// those; only the panel's own identity can.
'use strict';

const EDGE_WIDTH = 2560;
const EDGE_HEIGHT = 720;
const EDGE_ASPECT = EDGE_WIDTH / EDGE_HEIGHT;   // 32:9

function findEdgeDisplay(displays, primaryId) {
  if (!Array.isArray(displays) || displays.length === 0) return null;

  const exact = displays.find((d) => d.size.width === EDGE_WIDTH && d.size.height === EDGE_HEIGHT);
  if (exact) return exact;

  const candidates = displays.filter((d) => {
    const { width, height } = d.size;
    if (width < 2000 || height < 550) return false;   // an unrelated small display
    if (width > 4000) return false;                   // a 5120x1440 super-ultrawide
    if (d.id === primaryId) return false;             // where a 49 inch lives
    return Math.abs(width / height - EDGE_ASPECT) < 0.02;
  });
  candidates.sort((a, b) => Math.abs(a.size.width - EDGE_WIDTH) - Math.abs(b.size.width - EDGE_WIDTH));
  return candidates[0] || null;
}

module.exports = { findEdgeDisplay, EDGE_WIDTH, EDGE_HEIGHT, EDGE_ASPECT };
