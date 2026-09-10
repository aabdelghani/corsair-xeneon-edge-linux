// edgeline: named snapshots of everything the panel is set to.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The design says a profile stores "picture values, preset, RGB gain, touch
// mode, calibration matrix, dashboard layout". That is what this stores, one
// JSON file per profile under ~/.config/edgeline/profiles/.
//
// Files rather than one blob so a profile can be copied between machines, kept
// in a dotfiles repo, or hand-edited, and so a corrupt profile costs you one
// profile instead of all of them.
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace xen::profiles {

// Directory holding the profile files. Created on demand.
QString directory();

// Filesystem-safe name derived from a display name. Two profiles whose names
// differ only by punctuation would collide, so callers must check exists().
QString slugFor(const QString& name);

QStringList list();                          // display names, sorted
bool exists(const QString& name);
QJsonObject load(const QString& name);       // empty object when missing
bool save(const QString& name, const QJsonObject& body, QString* errorOut = nullptr);
bool remove(const QString& name, QString* errorOut = nullptr);
bool rename(const QString& from, const QString& to, QString* errorOut = nullptr);

// The profile applied at startup and shown in the titlebar.
QString active();
void setActive(const QString& name);

} // namespace xen::profiles
