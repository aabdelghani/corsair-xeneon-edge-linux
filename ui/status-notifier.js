// EdgeLine UI: wait for a StatusNotifier host before creating the tray icon.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Why: Chromium checks once, when the Tray is created, that
// org.kde.StatusNotifierWatcher has an owner and that
// IsStatusNotifierHostRegistered is true. If not, Electron falls back to a
// GtkStatusIcon (XEmbed) for the whole life of that Tray, which blocks VRR and
// direct scanout under GNOME. So main.js polls the property first, with an upper
// bound, and only then creates the Tray. No Electron import, so plain Node tests it.
'use strict';

const WATCHER = 'org.kde.StatusNotifierWatcher';
const TOOL_TIMEOUT_MS = 2000;

// gdbus prints the bare variant, dbus-send --print-reply a reply header plus a
// "variant boolean <value>" line. Only those two shapes count as true: a bare
// word or a quoted string must not pass for a registered host.
function parseHostRegistered(stdout) {
  if (typeof stdout !== 'string') return false;
  const s = stdout.trim();
  return /^\(<true>,\)$/.test(s) || /^variant\s+boolean\s+true$/m.test(s.replace(/^[ \t]+/gm, ''));
}

// Under Wayland the shell may still be loading the extension that hosts the
// tray, so give it longer. Under X11 an XEmbed-only tray (i3, trayer) is a
// legitimate target and the icon should not wait long for a host that never comes.
function waitBudgetMs(env) {
  return (env.XDG_SESSION_TYPE === 'wayland' || env.WAYLAND_DISPLAY) ? 15000 : 3000;
}

// Resolves { err, stdout } and never rejects; a synchronous throw from
// execFileImpl is treated like a failed run.
function runTool(execFileImpl, file, args) {
  return new Promise((resolve) => {
    try {
      execFileImpl(file, args, { timeout: TOOL_TIMEOUT_MS }, (err, stdout) => {
        resolve({ err, stdout });
      });
    } catch (err) {
      resolve({ err, stdout: '' });
    }
  });
}

const missing = (err) => !!err && err.code === 'ENOENT';

// One property read answers both of Chromium's checks: without a watcher owner
// the call fails with ServiceUnknown. Only a missing gdbus switches to dbus-send;
// any other failure (rc != 0, timeout) means "not there yet", not "no tool".
async function probeHostRegistered(execFileImpl = require('child_process').execFile) {
  let r = await runTool(execFileImpl, 'gdbus', [
    'call', '--session', '--dest', WATCHER, '--object-path', '/StatusNotifierWatcher',
    '--method', 'org.freedesktop.DBus.Properties.Get', WATCHER, 'IsStatusNotifierHostRegistered',
  ]);
  if (missing(r.err)) {
    r = await runTool(execFileImpl, 'dbus-send', [
      '--session', '--print-reply', '--dest=' + WATCHER, '/StatusNotifierWatcher',
      'org.freedesktop.DBus.Properties.Get', 'string:' + WATCHER,
      'string:IsStatusNotifierHostRegistered',
    ]);
    if (missing(r.err)) return 'unavailable';
  }
  if (r.err) return 'not-ready';
  return parseHostRegistered(r.stdout) ? 'ready' : 'not-ready';
}

// The last probe happens at the deadline itself, so a host that shows up in the
// final interval is still caught.
async function waitForStatusNotifierHost({ probe, budgetMs, intervalMs = 500, sleep, now = Date.now }) {
  const start = now();
  for (;;) {
    const r = await probe();
    if (r === 'ready') return { ready: true, reason: 'ready', waitedMs: now() - start };
    if (r === 'unavailable') return { ready: false, reason: 'unavailable', waitedMs: now() - start };
    const elapsed = now() - start;
    if (elapsed >= budgetMs) return { ready: false, reason: 'timeout', waitedMs: elapsed };
    await sleep(Math.min(intervalMs, budgetMs - elapsed));
  }
}

module.exports = {
  WATCHER,
  parseHostRegistered,
  waitBudgetMs,
  probeHostRegistered,
  waitForStatusNotifierHost,
};
