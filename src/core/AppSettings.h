// xeneon-ctl — small QSettings-backed persistence for touch mode, DDC values,
// and the login autostart entry (M7).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QMap>
#include <QString>

namespace xen {
namespace settings {

// Touch mode (stored as the TouchControl::Mode int value).
void saveTouchMode(int mode);
int loadTouchMode(int fallback);

// DDC/CI VCP values keyed by VCP code, so they can be restored at login.
void saveVcp(int code, int value);
QMap<int, int> loadVcps();

// Login autostart: writes/removes ~/.config/autostart/xeneon-ctl.desktop that
// runs the app with --restore.
bool autostartEnabled();
bool setAutostart(bool enabled, QString* errorOut = nullptr);

} // namespace settings
} // namespace xen
