// EdgeLine UI: StatusNotifier host wait. Pins parseHostRegistered, waitBudgetMs,
// probeHostRegistered (fake execFile) and waitForStatusNotifierHost (fake clock).
// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';

const assert = require('assert');
const sn = require('../status-notifier');

let count = 0;
function ok(label) {
  count += 1;
  console.log(`  ok  ${label}`);
}

// Replies captured from gdbus 2.88 and dbus-send on a GNOME session.
const GDBUS_TRUE = '(<true>,)\n';
const GDBUS_FALSE = '(<false>,)\n';
const DBUS_HEAD =
  'method return time=1791675174.613470 sender=:1.61 -> destination=:1.705 serial=13120 reply_serial=2\n';
const DBUS_TRUE = DBUS_HEAD + '   variant       boolean true\n';
const DBUS_FALSE = DBUS_HEAD + '   variant       boolean false\n';
const SERVICE_UNKNOWN =
  'Error: GDBus.Error:org.freedesktop.DBus.Error.ServiceUnknown: ' +
  'The name org.kde.StatusNotifierWatcher was not provided by any .service files\n';

const WATCHER_NAME = 'org.kde.StatusNotifierWatcher';
const GDBUS_ARGV = [
  'call', '--session', '--dest', WATCHER_NAME, '--object-path', '/StatusNotifierWatcher',
  '--method', 'org.freedesktop.DBus.Properties.Get', WATCHER_NAME, 'IsStatusNotifierHostRegistered',
];
const DBUS_SEND_ARGV = [
  '--session', '--print-reply', '--dest=' + WATCHER_NAME, '/StatusNotifierWatcher',
  'org.freedesktop.DBus.Properties.Get', 'string:' + WATCHER_NAME,
  'string:IsStatusNotifierHostRegistered',
];

// --- WATCHER constant ---------------------------------------------------------------------
assert.strictEqual(sn.WATCHER, WATCHER_NAME, 'WATCHER constant');
ok('WATCHER is org.kde.StatusNotifierWatcher');

// --- packaging: the module ships and its test runs -----------------------------------------
// build.files is a whitelist; a module missing there is absent from the .deb and AppImage
// and main.js would fail at require().
{
  const pkg = require('../package.json');
  assert.ok(pkg.build.files.includes('status-notifier.js'), 'build.files lists status-notifier.js');
  ok('package.json build.files ships status-notifier.js');
  assert.ok(/node test\/status-notifier\.test\.js/.test(pkg.scripts.test), 'scripts.test runs this test');
  ok('package.json scripts.test runs test/status-notifier.test.js');
}

// --- parseHostRegistered --------------------------------------------------------------------
const parseCases = [
  ['gdbus true', GDBUS_TRUE, true],
  ['gdbus false', GDBUS_FALSE, false],
  ['dbus-send true', DBUS_TRUE, true],
  ['dbus-send false', DBUS_FALSE, false],
  ['empty string', '', false],
  ['bare word true', 'true', false],
  ['quoted string "true"', '(<"true">,)', false],
  ['ServiceUnknown error text', SERVICE_UNKNOWN, false],
  ['gdbus true without newline', '(<true>,)', true],
  ['gdbus true, surrounding whitespace', '  \n\t(<true>,)\n\n  ', true],
  ['gdbus false, surrounding whitespace', '\n  (<false>,)  \n', false],
  ['dbus-send true, surrounding whitespace', '\n\n' + DBUS_TRUE + '  \n', true],
  ['dbus-send false, surrounding whitespace', '  ' + DBUS_FALSE + '\n\n', false],
  ['dbus-send true without trailing newline', DBUS_HEAD + '   variant       boolean true', true],
];
for (const [label, input, expected] of parseCases) {
  assert.strictEqual(sn.parseHostRegistered(input), expected, `parse ${label}: expected ${expected}`);
  ok(`parseHostRegistered: ${label} -> ${expected}`);
}

// --- waitBudgetMs ---------------------------------------------------------------------------
const budgetCases = [
  ['XDG_SESSION_TYPE=wayland', { XDG_SESSION_TYPE: 'wayland' }, 15000],
  ['WAYLAND_DISPLAY alone', { WAYLAND_DISPLAY: 'wayland-0' }, 15000],
  ['WAYLAND_DISPLAY set despite x11 session type', { XDG_SESSION_TYPE: 'x11', WAYLAND_DISPLAY: 'wayland-0' }, 15000],
  ['wayland session type with empty WAYLAND_DISPLAY', { XDG_SESSION_TYPE: 'wayland', WAYLAND_DISPLAY: '' }, 15000],
  ['empty WAYLAND_DISPLAY string', { WAYLAND_DISPLAY: '' }, 3000],
  ['x11', { XDG_SESSION_TYPE: 'x11' }, 3000],
  ['x11 with empty WAYLAND_DISPLAY', { XDG_SESSION_TYPE: 'x11', WAYLAND_DISPLAY: '' }, 3000],
  ['empty env', {}, 3000],
];
for (const [label, env, expected] of budgetCases) {
  assert.strictEqual(sn.waitBudgetMs(env), expected, `budget ${label}: expected ${expected}`);
  ok(`waitBudgetMs: ${label} -> ${expected}`);
}

// --- fakes ----------------------------------------------------------------------------------
// Fake execFile: handlers[file]() returns { err, stdout, stderr } or { throws }.
// The callback is invoked asynchronously, like the real execFile: cb(err, stdout, stderr).
function fakeExec(handlers) {
  const calls = [];
  const impl = (file, args, options, cb) => {
    calls.push({ file, args, options });
    const h = handlers[file];
    if (!h) throw new Error(`unexpected tool in test: ${file}`);
    const r = h();
    if (r.throws) throw r.throws;
    setImmediate(() => cb(r.err || null, r.stdout || '', r.stderr || ''));
  };
  return { impl, calls };
}
function enoent(tool) {
  return Object.assign(new Error(`spawn ${tool} ENOENT`), { code: 'ENOENT', errno: -2, syscall: `spawn ${tool}` });
}
const okOut = (stdout) => () => ({ stdout });
const failWith = (err, stderr) => () => ({ err, stderr: stderr || '' });

// Fake clock: sleep() records its argument and advances the counter. It starts at 1000,
// so a result that ignores the start time shows up.
function fakeClock(start = 1000) {
  let t = start;
  const sleeps = [];
  const events = [];
  return {
    sleeps,
    events,
    now: () => t,
    sleep: async (ms) => { sleeps.push(ms); events.push(`sleep:${ms}`); t += ms; },
  };
}
// Probe returning results[i] on call i (last value repeats).
function seqProbe(results, events) {
  const state = { calls: 0 };
  state.fn = async () => {
    const r = results[Math.min(state.calls, results.length - 1)];
    state.calls += 1;
    if (events) events.push('probe');
    return r;
  };
  return state;
}
const repeat = (n, v) => Array.from({ length: n }, () => v);

(async () => {
  // --- probeHostRegistered ------------------------------------------------------------------
  const PROBE_VALUES = ['ready', 'not-ready', 'unavailable'];

  {
    const f = fakeExec({ gdbus: okOut(GDBUS_TRUE), 'dbus-send': okOut(DBUS_TRUE) });
    assert.strictEqual(await sn.probeHostRegistered(f.impl), 'ready');
    assert.strictEqual(f.calls.length, 1, 'only gdbus is called');
    assert.strictEqual(f.calls[0].file, 'gdbus');
    assert.deepStrictEqual(f.calls[0].args, GDBUS_ARGV);
    assert.deepStrictEqual(f.calls[0].options, { timeout: 2000 });
    assert.ok(!f.calls.some((c) => c.file === 'dbus-send'), 'dbus-send not called');
    ok('probe: gdbus true -> ready, exact gdbus argv, timeout 2000, no dbus-send');
  }
  {
    const f = fakeExec({ gdbus: okOut(GDBUS_FALSE), 'dbus-send': okOut(DBUS_TRUE) });
    assert.strictEqual(await sn.probeHostRegistered(f.impl), 'not-ready');
    assert.strictEqual(f.calls.length, 1);
    assert.strictEqual(f.calls[0].file, 'gdbus');
    ok('probe: gdbus false -> not-ready, no dbus-send');
  }
  {
    const f = fakeExec({ gdbus: okOut(''), 'dbus-send': okOut(DBUS_TRUE) });
    assert.strictEqual(await sn.probeHostRegistered(f.impl), 'not-ready');
    assert.strictEqual(f.calls.length, 1);
    ok('probe: gdbus success with empty stdout -> not-ready, no dbus-send');
  }
  {
    const f = fakeExec({ gdbus: failWith(enoent('gdbus')), 'dbus-send': okOut(DBUS_TRUE) });
    assert.strictEqual(await sn.probeHostRegistered(f.impl), 'ready');
    assert.strictEqual(f.calls.length, 2);
    assert.strictEqual(f.calls[0].file, 'gdbus');
    assert.strictEqual(f.calls[1].file, 'dbus-send');
    assert.deepStrictEqual(f.calls[1].args, DBUS_SEND_ARGV);
    assert.deepStrictEqual(f.calls[1].options, { timeout: 2000 });
    ok('probe: gdbus ENOENT -> dbus-send true -> ready, exact dbus-send argv, timeout 2000');
  }
  {
    const f = fakeExec({ gdbus: failWith(enoent('gdbus')), 'dbus-send': okOut(DBUS_FALSE) });
    assert.strictEqual(await sn.probeHostRegistered(f.impl), 'not-ready');
    assert.strictEqual(f.calls.length, 2);
    assert.strictEqual(f.calls[1].file, 'dbus-send');
    assert.deepStrictEqual(f.calls[1].args, DBUS_SEND_ARGV);
    ok('probe: gdbus ENOENT -> dbus-send false -> not-ready');
  }
  {
    const err = Object.assign(new Error('Command failed: gdbus call ...'), { code: 1 });
    const f = fakeExec({ gdbus: failWith(err, SERVICE_UNKNOWN), 'dbus-send': okOut(DBUS_TRUE) });
    assert.strictEqual(await sn.probeHostRegistered(f.impl), 'not-ready');
    assert.strictEqual(f.calls.length, 1, 'no switch to dbus-send on rc 1');
    assert.ok(!f.calls.some((c) => c.file === 'dbus-send'));
    ok('probe: gdbus rc 1 (ServiceUnknown) -> not-ready, no dbus-send');
  }
  {
    const err = Object.assign(new Error('Command failed: gdbus call ...'), { code: 127 });
    const f = fakeExec({ gdbus: failWith(err), 'dbus-send': okOut(DBUS_TRUE) });
    assert.strictEqual(await sn.probeHostRegistered(f.impl), 'not-ready');
    assert.strictEqual(f.calls.length, 1, 'only err.code === ENOENT switches tools');
    ok('probe: gdbus rc 127 (not ENOENT) -> not-ready, no dbus-send');
  }
  {
    const err = Object.assign(new Error('Command failed: gdbus call ...'), { killed: true, signal: 'SIGTERM', code: null });
    const f = fakeExec({ gdbus: failWith(err), 'dbus-send': okOut(DBUS_TRUE) });
    assert.strictEqual(await sn.probeHostRegistered(f.impl), 'not-ready');
    assert.strictEqual(f.calls.length, 1, 'no fallback on timeout');
    ok('probe: gdbus timeout (killed, SIGTERM) -> not-ready, no fallback');
  }
  {
    const f = fakeExec({ gdbus: failWith(enoent('gdbus')), 'dbus-send': failWith(enoent('dbus-send')) });
    assert.strictEqual(await sn.probeHostRegistered(f.impl), 'unavailable');
    assert.deepStrictEqual(f.calls.map((c) => c.file), ['gdbus', 'dbus-send']);
    ok('probe: both tools ENOENT -> unavailable');
  }
  {
    const err = Object.assign(new Error('Command failed: dbus-send ...'), { code: 1 });
    const f = fakeExec({ gdbus: failWith(enoent('gdbus')), 'dbus-send': failWith(err, SERVICE_UNKNOWN) });
    assert.strictEqual(await sn.probeHostRegistered(f.impl), 'not-ready');
    assert.strictEqual(f.calls.length, 2);
    ok('probe: gdbus ENOENT, dbus-send rc 1 -> not-ready');
  }
  {
    const err = Object.assign(new Error('Command failed: dbus-send ...'), { killed: true, signal: 'SIGTERM', code: null });
    const f = fakeExec({ gdbus: failWith(enoent('gdbus')), 'dbus-send': failWith(err) });
    assert.strictEqual(await sn.probeHostRegistered(f.impl), 'not-ready');
    assert.strictEqual(f.calls.length, 2);
    ok('probe: gdbus ENOENT, dbus-send timeout -> not-ready');
  }
  {
    const f = fakeExec({ gdbus: failWith(new Error('boom')), 'dbus-send': okOut(DBUS_TRUE) });
    assert.strictEqual(await sn.probeHostRegistered(f.impl), 'not-ready');
    assert.strictEqual(f.calls.length, 1);
    ok('probe: gdbus error without code -> not-ready, no fallback');
  }
  {
    const f = fakeExec({ gdbus: failWith({}), 'dbus-send': okOut(DBUS_TRUE) });
    assert.strictEqual(await sn.probeHostRegistered(f.impl), 'not-ready');
    assert.strictEqual(f.calls.length, 1);
    ok('probe: gdbus plain-object error -> not-ready, no fallback');
  }
  {
    // The probe never throws: it always resolves to one of its three values.
    const f = fakeExec({ gdbus: () => ({ throws: new Error('boom') }), 'dbus-send': okOut(DBUS_TRUE) });
    const r = await sn.probeHostRegistered(f.impl);
    assert.ok(PROBE_VALUES.includes(r), `unexpected probe value ${r}`);
    ok('probe: execFile throws synchronously (gdbus) -> still resolves to one of the three values');
  }
  {
    const f = fakeExec({
      gdbus: failWith(enoent('gdbus')),
      'dbus-send': () => ({ throws: new TypeError('bad arguments') }),
    });
    const r = await sn.probeHostRegistered(f.impl);
    assert.ok(PROBE_VALUES.includes(r), `unexpected probe value ${r}`);
    ok('probe: execFile throws synchronously (dbus-send) -> still resolves to one of the three values');
  }

  // --- waitForStatusNotifierHost ------------------------------------------------------------
  {
    const clock = fakeClock();
    const p = seqProbe(['not-ready'], clock.events);
    const r = await sn.waitForStatusNotifierHost({
      probe: p.fn, budgetMs: 3000, intervalMs: 500, sleep: clock.sleep, now: clock.now,
    });
    assert.strictEqual(p.calls, 7);
    assert.deepStrictEqual(clock.sleeps, repeat(6, 500));
    assert.deepStrictEqual(r, { ready: false, reason: 'timeout', waitedMs: 3000 });
    assert.strictEqual(clock.events[0], 'probe', 'first probe comes before any sleep');
    ok('wait: budget 3000 / interval 500, always not-ready -> 7 probes, 6x500 sleeps, timeout, 3000 ms');
  }
  {
    const clock = fakeClock();
    const p = seqProbe(['not-ready'], clock.events);
    const r = await sn.waitForStatusNotifierHost({
      probe: p.fn, budgetMs: 1200, intervalMs: 500, sleep: clock.sleep, now: clock.now,
    });
    assert.strictEqual(p.calls, 4);
    assert.deepStrictEqual(clock.sleeps, [500, 500, 200]);
    assert.deepStrictEqual(r, { ready: false, reason: 'timeout', waitedMs: 1200 });
    assert.deepStrictEqual(clock.events, ['probe', 'sleep:500', 'probe', 'sleep:500', 'probe', 'sleep:200', 'probe']);
    ok('wait: budget 1200 / interval 500 -> 4 probes, sleeps 500,500,200, timeout, 1200 ms');
  }
  {
    const clock = fakeClock();
    const p = seqProbe(['not-ready', 'not-ready', 'ready']);
    const r = await sn.waitForStatusNotifierHost({
      probe: p.fn, budgetMs: 15000, intervalMs: 500, sleep: clock.sleep, now: clock.now,
    });
    assert.strictEqual(p.calls, 3);
    assert.deepStrictEqual(clock.sleeps, [500, 500]);
    assert.deepStrictEqual(r, { ready: true, reason: 'ready', waitedMs: 1000 });
    ok('wait: ready on third probe -> 2x500 sleep, ready, 1000 ms');
  }
  {
    const clock = fakeClock();
    const p = seqProbe(['ready']);
    const r = await sn.waitForStatusNotifierHost({
      probe: p.fn, budgetMs: 15000, intervalMs: 500, sleep: clock.sleep, now: clock.now,
    });
    assert.strictEqual(p.calls, 1);
    assert.deepStrictEqual(clock.sleeps, []);
    assert.deepStrictEqual(r, { ready: true, reason: 'ready', waitedMs: 0 });
    ok('wait: ready on first probe -> zero sleeps, waitedMs 0');
  }
  {
    const clock = fakeClock();
    const p = seqProbe(['unavailable']);
    const r = await sn.waitForStatusNotifierHost({
      probe: p.fn, budgetMs: 15000, intervalMs: 500, sleep: clock.sleep, now: clock.now,
    });
    assert.strictEqual(p.calls, 1);
    assert.deepStrictEqual(clock.sleeps, []);
    assert.deepStrictEqual(r, { ready: false, reason: 'unavailable', waitedMs: 0 });
    ok('wait: unavailable on first probe -> zero sleeps, unavailable, waitedMs 0');
  }
  {
    const clock = fakeClock();
    const p = seqProbe(['not-ready', 'unavailable']);
    const r = await sn.waitForStatusNotifierHost({
      probe: p.fn, budgetMs: 15000, intervalMs: 500, sleep: clock.sleep, now: clock.now,
    });
    assert.strictEqual(p.calls, 2);
    assert.deepStrictEqual(clock.sleeps, [500]);
    assert.deepStrictEqual(r, { ready: false, reason: 'unavailable', waitedMs: 500 });
    ok('wait: not-ready then unavailable -> stops after one sleep, unavailable, 500 ms');
  }
  {
    const clock = fakeClock();
    const p = seqProbe(['not-ready']);
    const r = await sn.waitForStatusNotifierHost({
      probe: p.fn, budgetMs: 1200, sleep: clock.sleep, now: clock.now,
    });
    assert.deepStrictEqual(clock.sleeps, [500, 500, 200]);
    assert.strictEqual(p.calls, 4);
    assert.deepStrictEqual(r, { ready: false, reason: 'timeout', waitedMs: 1200 });
    ok('wait: intervalMs defaults to 500');
  }
  {
    const clock = fakeClock();
    const p = seqProbe(['not-ready']);
    const r = await sn.waitForStatusNotifierHost({
      probe: p.fn, budgetMs: 2500, intervalMs: 1000, sleep: clock.sleep, now: clock.now,
    });
    assert.deepStrictEqual(clock.sleeps, [1000, 1000, 500]);
    assert.strictEqual(p.calls, 4);
    assert.deepStrictEqual(r, { ready: false, reason: 'timeout', waitedMs: 2500 });
    ok('wait: custom intervalMs 1000, budget 2500 -> sleeps 1000,1000,500, timeout, 2500 ms');
  }

  console.log(`status-notifier: all ${count} cases pass`);
})().catch((e) => { console.error(e); process.exit(1); });
