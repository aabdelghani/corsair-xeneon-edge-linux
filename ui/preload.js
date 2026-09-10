// EdgeLine UI: the entire surface the renderer is allowed to touch.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The renderer runs sandboxed with no Node. Everything it can do is listed
// here, and every hardware operation goes through one generic `call`, so
// adding an agent method needs no change on this side.
'use strict';

const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('edgeline', {
  // Agent RPC. Rejects with the agent's own message on failure.
  call: (method, params) => ipcRenderer.invoke('rpc', method, params || {}),

  // Connection state, and a subscription for changes to it.
  connected: () => ipcRenderer.invoke('agent-connected'),
  onStatus: (cb) => ipcRenderer.on('agent-status', (_e, st) => cb(st)),

  // Unsolicited agent events: ddc, ddcLog, device, touch, touch.point,
  // sensors, update.
  onEvent: (cb) => ipcRenderer.on('agent-event', (_e, msg) => cb(msg)),

  // Chrome for the frameless window the design draws itself.
  windowAction: (action) => ipcRenderer.invoke('window-action', action),

  openExternal: (url) => ipcRenderer.invoke('open-external', url),

  // Windows that live on the panel itself.
  dashboard: (on) => ipcRenderer.invoke('dashboard', on),
  dashboardState: () => ipcRenderer.invoke('dashboard-state'),
  dashboardLayout: (layout) => ipcRenderer.invoke('dashboard-layout', layout),
  fitToContent: (h) => ipcRenderer.invoke('fit-to-content', h),
  openCalibration: () => ipcRenderer.invoke('open-calibration'),
  closeCalibration: () => ipcRenderer.invoke('close-calibration'),
  ripple: (on) => ipcRenderer.invoke('ripple', on),
  edgeDisplay: () => ipcRenderer.invoke('edge-display'),
  appInfo: () => ipcRenderer.invoke('app-info'),
});
