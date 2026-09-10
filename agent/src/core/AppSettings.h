// edgeline: small QSettings-backed persistence for touch mode, DDC values,
// and the login autostart entry (M7).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QMap>
#include <QString>


namespace xen::settings {

// Call once, before any other settings use, and before QSettings is touched.
// Sets the organisation and application names, and carries a pre-rename
// xeneon-ctl config across to ~/.config/edgeline/ the first time.
// Returns true when a migration actually happened.
bool init();

// Absolute path of the config file in use. Shown on the Settings page.
QString configPath();

// Touch mode (stored as the TouchControl::Mode int value).
void saveTouchMode(int mode);
int loadTouchMode(int fallback);

// DDC/CI VCP values keyed by VCP code, so they can be restored at login.
void saveVcp(int code, int value);
QMap<int, int> loadVcps();

// Login autostart: writes/removes ~/.config/autostart/edgeline.desktop that
// runs the app with --restore.
bool autostartEnabled();
bool setAutostart(bool enabled, QString* errorOut = nullptr);

} // namespace xen::settings

