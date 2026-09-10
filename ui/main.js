// Edgeline UI: Electron main process.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This process renders and relays. It never touches the panel: DDC, xinput,
// XI2 and hidraw all live in edgeline-agent, and everything here goes through
// the socket. Two windows (this one and the dashboard on the panel) therefore
// cannot fight over one i2c bus.
'use strict';

const { app, BrowserWindow, ipcMain, screen, shell, nativeTheme } = require('electron');
const { execFile, spawn } = require('child_process');
const fs = require('fs');
const net = require('net');
const os = require('os');
const path = require('path');

const AGENT_NAME = 'edgeline-agent';
const SOCKET = path.join(
  process.env.XDG_RUNTIME_DIR || `/run/user/${os.userInfo().uid}`,
  'edgeline.sock'
);
const RPC_TIMEOUT_MS = 8000;
const RECONNECT_MS = 1500;
const MAX_AUTO_RESTARTS = 3;

let mainWindow = null;
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
  mainWindow.loadFile(path.join(__dirname, 'renderer', 'index.html'));
  mainWindow.once('ready-to-show', () => mainWindow.show());
  mainWindow.on('closed', () => { mainWindow = null; });
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
    createMainWindow();
    app.on('activate', () => {
      if (BrowserWindow.getAllWindows().length === 0) createMainWindow();
    });
  });

  app.on('window-all-closed', () => app.quit());
  app.on('before-quit', () => {
    if (reconnectTimer) clearTimeout(reconnectTimer);
    if (sock) sock.destroy();
  });
}
