// EdgeLine UI: Electron main process.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This process renders and relays. It never touches the panel: DDC, xinput,
// XI2 and hidraw all live in edgeline-agent, and everything here goes through
// the socket. Two windows (this one and the dashboard on the panel) therefore
// cannot fight over one i2c bus.
'use strict';

const { app, BrowserWindow, Menu, Tray, ipcMain, screen, shell, nativeTheme,
        Notification } = require('electron');
const { execFile, spawn } = require('child_process');
const fs = require('fs');
const net = require('net');
const os = require('os');
const path = require('path');

const AGENT_NAME = 'edgeline-agent';
const APP_ICON = path.join(__dirname, 'assets', 'icons', '256x256.png');
const SOCKET = path.join(
  process.env.XDG_RUNTIME_DIR || `/run/user/${os.userInfo().uid}`,
  'edgeline.sock'
);
const RPC_TIMEOUT_MS = 8000;
const RECONNECT_MS = 1500;
const MAX_AUTO_RESTARTS = 3;

let mainWindow = null;
let tray = null;
let quitting = false;
let trayHintShown = false;

// A small mirror of agent state, kept only so the tray menu can show what is
// currently true without a round trip every time it is opened.
const agentState = { touchMode: null, profiles: [], activeProfile: '', blanked: false };
let sock = null;
let buffer = '';
let nextId = 1;
const pending = new Map();
let connected = false;
let autoRestarts = 0;
let reconnectTimer = null;

// ---------------------------------------------------------------- agent bin

// Ordered by specificity: a packaged copy first, then a system install, then a
// developer's build tree. Anything not executable is skipped rather than
// spawned and failed.
function agentCandidates() {
  return [
    path.join(process.resourcesPath || '', 'agent', AGENT_NAME),
    `/usr/bin/${AGENT_NAME}`,
    `/usr/local/bin/${AGENT_NAME}`,
    path.join(os.homedir(), '.local', 'bin', AGENT_NAME),
    path.join(__dirname, '..', 'agent', 'build', AGENT_NAME),
  ].filter((p) => {
    try {
      fs.accessSync(p, fs.constants.X_OK);
      return true;
    } catch {
      return false;
    }
  });
}

function startAgent() {
  const bin = agentCandidates()[0];
  if (!bin) return false;
  try {
    // detached:false so the agent dies with the UI when run from a checkout;
    // a packaged install uses the systemd user unit instead.
    const child = spawn(bin, [], { stdio: 'ignore' });
    child.unref();
    return true;
  } catch {
    return false;
  }
}

// ---------------------------------------------------------------- transport

function notify(channel, payload) {
  for (const w of BrowserWindow.getAllWindows()) {
    if (!w.isDestroyed()) w.webContents.send(channel, payload);
  }
}

function failAllPending(reason) {
  for (const [, p] of pending) p.reject(new Error(reason));
  pending.clear();
}

function scheduleReconnect() {
  if (reconnectTimer) return;
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    connect();
  }, RECONNECT_MS);
}

function connect() {
  if (sock) return;
  sock = net.createConnection(SOCKET);
  sock.setEncoding('utf8');

  sock.on('connect', () => {
    connected = true;
    autoRestarts = 0;
    buffer = '';
    notify('agent-status', { connected: true, socket: SOCKET });
    primeTrayState();
  });

  sock.on('data', (chunk) => {
    buffer += chunk;
    let nl;
    while ((nl = buffer.indexOf('\n')) >= 0) {
      const line = buffer.slice(0, nl);
      buffer = buffer.slice(nl + 1);
      if (!line.trim()) continue;
      let msg;
      try {
        msg = JSON.parse(line);
      } catch {
        continue; // a malformed line must not kill the connection
      }
      if (msg.event) {
        notify('agent-event', msg);
        noteAgentEvent(msg);
        continue;
      }
      const p = pending.get(msg.id);
      if (!p) continue;
      pending.delete(msg.id);
      clearTimeout(p.timer);
      if (msg.error) p.reject(new Error(msg.error));
      else p.resolve(msg.result ?? {});
    }
  });

  const drop = () => {
    const wasConnected = connected;
    connected = false;
    if (sock) sock.destroy();
    sock = null;
    failAllPending('agent disconnected');
    notify('agent-status', { connected: false, socket: SOCKET });
    refreshTray();
    // Only try to start the agent when it was never up, and only a few times,
    // so a crash loop is not amplified into a spawn loop.
    if (!wasConnected && autoRestarts < MAX_AUTO_RESTARTS) {
      autoRestarts += 1;
      startAgent();
    }
    scheduleReconnect();
  };

  sock.on('error', drop);
  sock.on('close', drop);
}

function rpc(method, params) {
  return new Promise((resolve, reject) => {
    if (!sock || !connected) {
      reject(new Error('agent not connected'));
      return;
    }
    const id = nextId++;
    const timer = setTimeout(() => {
      if (pending.has(id)) {
        pending.delete(id);
        reject(new Error(`timeout calling ${method}`));
      }
    }, RPC_TIMEOUT_MS);
    pending.set(id, { resolve, reject, timer });
    sock.write(JSON.stringify({ id, method, params: params || {} }) + '\n');
  });
}

// Development switches used by shot.sh to capture a specific page and theme on
// a private display. They only preselect UI state; nothing here is privileged.
function devSwitch(name) {
  const pref = `--edgeline-${name}=`;
  const arg = process.argv.find((a) => a.startsWith(pref));
  return arg ? arg.slice(pref.length) : null;
}

// ---------------------------------------------------------------- windows

// The Edge is found by its resolution, never by a remembered connector name:
// the connector index changes across reboots and recables on this hardware.
function edgeDisplay() {
  return screen.getAllDisplays().find(
    (d) => d.size.width === 2560 && d.size.height === 720
  ) || null;
}

function createMainWindow() {
  // shot.sh can ask for a taller window so a whole page fits in one capture.
  const devH = parseInt(devSwitch('height') || '', 10);
  mainWindow = new BrowserWindow({
    icon: APP_ICON,
    width: 1200,
    height: Number.isFinite(devH) && devH > 0 ? devH : 760,
    minWidth: 940,
    minHeight: 620,
    frame: false,               // the design draws its own titlebar
    backgroundColor: '#1e1e1e', // ubuntu-dark --bg, so there is no white flash
    show: false,
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });
  mainWindow.removeMenu();
  // Renderer errors are otherwise invisible when the app runs under systemd.
  mainWindow.webContents.on('console-message', (_e, level, message, line, source) => {
    if (level >= 2) console.error(`[renderer] ${source}:${line} ${message}`);
  });
  mainWindow.loadFile(path.join(__dirname, 'renderer', 'index.html'));
  mainWindow.once('ready-to-show', () => { mainWindow.show(); refreshTray(); });
  mainWindow.on('show', refreshTray);
  mainWindow.on('hide', refreshTray);

  mainWindow.on('close', (e) => {
    // With a tray present, closing hides. Quitting outright would stop the
    // dashboard on the panel too, which is rarely what closing a window means.
    if (quitting || !tray) return;
    e.preventDefault();
    mainWindow.hide();
    refreshTray();
    if (!trayHintShown) {
      trayHintShown = true;
      if (Notification.isSupported()) {
        new Notification({
          title: 'EdgeLine is still running',
          body: 'It is in the top bar. Quit it from there when you are done.',
          icon: APP_ICON,
        }).show();
      }
    }
  });

  mainWindow.on('closed', () => { mainWindow = null; });
}

let dashWindow = null;
let calWindow = null;
let rippleWindow = null;

// Both of these live on the panel itself, so they are placed by finding a
// display of exactly 2560x720. The connector name is never used: it changes
// between reboots on this hardware.
function openDashboardWindow() {
  const edge = edgeDisplay();
  if (!edge) return { ok: false, error: 'the Edge is not attached to this session' };
  if (dashWindow) { dashWindow.show(); return { ok: true }; }

  dashWindow = new BrowserWindow({
    icon: APP_ICON,
    x: edge.bounds.x, y: edge.bounds.y,
    width: edge.bounds.width, height: edge.bounds.height,
    frame: false, alwaysOnTop: true, skipTaskbar: true, resizable: false,
    backgroundColor: '#171717',   // --strip, so there is no white flash
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true, nodeIntegration: false, sandbox: true,
    },
  });
  dashWindow.removeMenu();
  dashWindow.setAlwaysOnTop(true, 'normal');
  dashWindow.loadFile(path.join(__dirname, 'renderer', 'dashboard.html'));
  dashWindow.on('closed', () => { dashWindow = null; });
  return { ok: true };
}

function closeDashboardWindow() {
  if (dashWindow) dashWindow.close();
  dashWindow = null;
}

function openCalibrationWindow() {
  const edge = edgeDisplay();
  if (!edge) return { ok: false, error: 'the Edge is not attached to this session' };
  if (calWindow) { calWindow.focus(); return { ok: true }; }

  calWindow = new BrowserWindow({
    icon: APP_ICON,
    x: edge.bounds.x, y: edge.bounds.y,
    width: edge.bounds.width, height: edge.bounds.height,
    frame: false, fullscreen: false, alwaysOnTop: true, skipTaskbar: true,
    backgroundColor: '#000000',
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true, nodeIntegration: false, sandbox: true,
    },
  });
  calWindow.removeMenu();
  calWindow.setAlwaysOnTop(true, 'screen-saver');
  calWindow.loadFile(path.join(__dirname, 'renderer', 'calibrate.html'));
  calWindow.on('closed', () => { calWindow = null; });
  return { ok: true, bounds: edge.bounds, scale: edge.scaleFactor };
}

function openRippleWindow() {
  const edge = edgeDisplay();
  if (!edge) return { ok: false, error: 'the Edge is not attached to this session' };
  if (rippleWindow) return { ok: true };

  rippleWindow = new BrowserWindow({
    icon: APP_ICON,
    x: edge.bounds.x, y: edge.bounds.y,
    width: edge.bounds.width, height: edge.bounds.height,
    frame: false, transparent: true, alwaysOnTop: true, skipTaskbar: true,
    focusable: false, hasShadow: false, resizable: false,
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true, nodeIntegration: false, sandbox: true,
    },
  });
  rippleWindow.removeMenu();
  rippleWindow.setAlwaysOnTop(true, 'screen-saver');
  // Click-through: the overlay is feedback, not a target. Without this it
  // would swallow every click on the panel underneath it.
  rippleWindow.setIgnoreMouseEvents(true, { forward: true });
  rippleWindow.loadFile(path.join(__dirname, 'renderer', 'ripple.html'));
  rippleWindow.on('closed', () => { rippleWindow = null; });
  return { ok: true };
}

function closeRippleWindow() {
  if (rippleWindow) rippleWindow.close();
  rippleWindow = null;
}

// ---------------------------------------------------------------- tray

function trayIconPath() {
  // 24px reads correctly in the GNOME top bar; larger sizes get downscaled
  // badly by the indicator extension.
  return path.join(__dirname, 'assets', 'icons', '24x24.png');
}

function showMainWindow() {
  if (!mainWindow) { createMainWindow(); return; }
  if (mainWindow.isMinimized()) mainWindow.restore();
  mainWindow.show();
  mainWindow.focus();
}

const TOUCH_MODES = [
  ['off', 'Off'],
  ['main-cursor', 'Main cursor'],
  ['own-pointer', 'Own pointer'],
  ['ripple', 'Ripple only'],
];

function buildTrayMenu() {
  const connected = connected_();
  const items = [
    { label: mainWindow && mainWindow.isVisible() ? 'Hide window' : 'Show window',
      click: () => {
        if (mainWindow && mainWindow.isVisible()) mainWindow.hide();
        else showMainWindow();
      } },
    { type: 'separator' },
  ];

  if (!connected) {
    items.push({ label: 'Agent not running', enabled: false });
  } else {
    items.push({
      label: 'Touch mode',
      submenu: TOUCH_MODES.map(([id, label]) => ({
        label,
        type: 'radio',
        checked: agentState.touchMode === id,
        click: () => rpc('touch.setMode', { mode: id }).catch(() => {}),
      })),
    });

    items.push({
      label: 'Profile',
      enabled: agentState.profiles.length > 0,
      submenu: agentState.profiles.length
        ? agentState.profiles.map((name) => ({
            label: name,
            type: 'radio',
            checked: agentState.activeProfile === name,
            click: () => rpc('profiles.apply', { name }).catch(() => {}),
          }))
        : [{ label: 'No profiles saved', enabled: false }],
    });

    items.push({
      label: 'Dashboard on panel',
      type: 'checkbox',
      checked: !!dashWindow,
      enabled: !!edgeDisplay(),
      click: () => { if (dashWindow) closeDashboardWindow(); else openDashboardWindow(); refreshTray(); },
    });

    items.push({
      label: 'Blank panel',
      type: 'checkbox',
      checked: agentState.blanked,
      // 0x01 is on, 0x05 is the write-only "turn the display off" value.
      click: () => rpc('ddc.set', { code: 0xd6, value: agentState.blanked ? 0x01 : 0x05 })
        .catch(() => {}),
    });
  }

  items.push({ type: 'separator' });
  items.push({ label: 'Quit EdgeLine', click: () => { quitting = true; app.quit(); } });
  return Menu.buildFromTemplate(items);
}

function refreshTray() {
  if (!tray) return;
  tray.setContextMenu(buildTrayMenu());
  tray.setToolTip(connected_()
    ? `EdgeLine${agentState.activeProfile ? ' - ' + agentState.activeProfile : ''}`
    : 'EdgeLine (agent not running)');
}

function connected_() { return connected; }

function createTray() {
  if (tray) return;
  try {
    tray = new Tray(trayIconPath());
  } catch (err) {
    // Swallowing this made a missing tray look like a working one. If there is
    // no status notifier host the app still works, but say so.
    console.error(`[tray] could not create a tray icon: ${err.message}`);
    tray = null;
    return;
  }
  console.error('[tray] created');
  tray.setTitle('');
  // On GNOME the indicator has no separate click event, so the menu is the
  // whole interaction.
  tray.on('click', () => showMainWindow());
  refreshTray();
}

// The tray menu shows live state, so it follows the same events the UI does.
function noteAgentEvent(msg) {
  const d = msg.data || {};
  let dirty = false;
  if (msg.event === 'touch' && d.mode !== agentState.touchMode) {
    agentState.touchMode = d.mode;
    dirty = true;
  }
  if (msg.event === 'ddc' && d.values && d.values.d6) {
    const blanked = d.values.d6.value !== 1;
    if (blanked !== agentState.blanked) { agentState.blanked = blanked; dirty = true; }
  }
  if (msg.event === 'profiles') {
    refreshProfilesForTray();
    dirty = true;
  }
  if (dirty) refreshTray();
}

async function refreshProfilesForTray() {
  try {
    const r = await rpc('profiles.list');
    agentState.profiles = (r.profiles || []).map((p) => p.name);
    agentState.activeProfile = r.active || '';
  } catch {
    agentState.profiles = [];
  }
  refreshTray();
}

async function primeTrayState() {
  try {
    const all = await rpc('state.all');
    agentState.touchMode = (all.touch || {}).mode || null;
    const d6 = ((all.ddc || {}).values || {}).d6;
    agentState.blanked = d6 ? d6.value !== 1 : false;
  } catch { /* the tray simply shows less */ }
  await refreshProfilesForTray();
}

// ---------------------------------------------------------------- ipc

ipcMain.handle('rpc', (_e, method, params) => rpc(method, params));
ipcMain.handle('agent-connected', () => ({ connected, socket: SOCKET }));

ipcMain.handle('window-action', (e, action) => {
  const w = BrowserWindow.fromWebContents(e.sender);
  if (!w) return false;
  if (action === 'minimize') w.minimize();
  else if (action === 'maximize') w.isMaximized() ? w.unmaximize() : w.maximize();
  else if (action === 'close') w.close();
  else return false;
  return true;
});

ipcMain.handle('open-external', (_e, url) => {
  // Only ever hand http(s) to the desktop, never a file: or a shell string.
  if (typeof url !== 'string' || !/^https?:\/\//i.test(url)) return false;
  shell.openExternal(url);
  return true;
});

ipcMain.handle('dashboard', (_e, on) => {
  if (on) return openDashboardWindow();
  closeDashboardWindow();
  return { ok: true };
});
ipcMain.handle('dashboard-state', () => ({ open: !!dashWindow }));
ipcMain.handle('dashboard-layout', (_e, layout) => {
  if (dashWindow && !dashWindow.isDestroyed())
    dashWindow.webContents.send('agent-event', { event: 'dashboard', data: layout });
  return true;
});

ipcMain.handle('open-calibration', () => openCalibrationWindow());
ipcMain.handle('close-calibration', (e) => {
  const w = BrowserWindow.fromWebContents(e.sender);
  if (w && w === calWindow) w.close();
  else if (calWindow) calWindow.close();
  return true;
});
ipcMain.handle('ripple', (_e, on) => (on ? openRippleWindow() : (closeRippleWindow(), { ok: true })));
ipcMain.handle('edge-display', () => {
  const d = edgeDisplay();
  return d ? { bounds: d.bounds, scale: d.scaleFactor, workArea: d.workArea } : null;
});

ipcMain.handle('app-info', () => ({
  startTab: devSwitch('tab'),
  startTheme: devSwitch('theme'),
  version: app.getVersion(),
  electron: process.versions.electron,
  chrome: process.versions.chrome,
  node: process.versions.node,
  edgePresent: !!edgeDisplay(),
  prefersDark: nativeTheme.shouldUseDarkColors,
}));

// ---------------------------------------------------------------- lifecycle

if (!app.requestSingleInstanceLock()) {
  app.quit();
} else {
  app.on('second-instance', () => {
    if (mainWindow) {
      if (mainWindow.isMinimized()) mainWindow.restore();
      mainWindow.focus();
    }
  });

  app.whenReady().then(() => {
    connect();
    // Nothing is listening on a fresh machine, so bring the agent up rather
    // than showing a window that can do nothing.
    if (!fs.existsSync(SOCKET)) startAgent();
    createTray();
    createMainWindow();
    app.on('activate', () => {
      if (BrowserWindow.getAllWindows().length === 0) createMainWindow();
    });
  });

  // Not app.quit(): with a tray the app deliberately outlives its windows.
  app.on('window-all-closed', () => { if (!tray) app.quit(); });
  app.on('before-quit', () => { quitting = true; });
  app.on('before-quit', () => {
    if (reconnectTimer) clearTimeout(reconnectTimer);
    if (sock) sock.destroy();
  });
}
