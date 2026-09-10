// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTextStream>


namespace xen::settings {

bool init()
{
    QCoreApplication::setOrganizationName(QStringLiteral("edgeline"));
    QCoreApplication::setApplicationName(QStringLiteral("edgeline"));

    const QString target = QSettings().fileName();
    if (QFile::exists(target))
        return false;

    // The project was called xeneon-ctl until 0.4.0. Someone upgrading has
    // brightness, a touch mode and update preferences in the old file; losing
    // them on a rename would be a rude way to introduce a new name.
    const QString legacy = QDir::homePath()
        + QStringLiteral("/.config/xeneon-ctl/xeneon-ctl.conf");
    if (!QFile::exists(legacy))
        return false;

    QDir().mkpath(QFileInfo(target).absolutePath());
    if (!QFile::copy(legacy, target))
        return false;
    // The old file is left alone: if this turns out badly the user still has it.
    return true;
}

QString configPath()
{
    return QSettings().fileName();
}

void saveTouchMode(int mode)
{
    QSettings().setValue(QStringLiteral("touch/mode"), mode);
}

int loadTouchMode(int fallback)
{
    return QSettings().value(QStringLiteral("touch/mode"), fallback).toInt();
}

void saveVcp(int code, int value)
{
    QSettings s;
    s.setValue(QStringLiteral("ddc/vcp_%1").arg(code, 2, 16, QLatin1Char('0')), value);
}

QMap<int, int> loadVcps()
{
    QMap<int, int> out;
    QSettings s;
    s.beginGroup(QStringLiteral("ddc"));
    for (const QString& key : s.childKeys()) {
        bool ok = false;
        const int code = QStringView{key}.mid(4).toInt(&ok, 16); // "vcp_XX"
        if (ok)
            out.insert(code, s.value(key).toInt());
    }
    s.endGroup();
    return out;
}

namespace {
QString autostartPath()
{
    return QDir::homePath() + QStringLiteral("/.config/autostart/edgeline.desktop");
}
} // namespace

bool autostartEnabled()
{
    return QFile::exists(autostartPath());
}

bool setAutostart(bool enabled, QString* errorOut)
{
    const QString path = autostartPath();
    if (!enabled) {
        if (QFile::exists(path) && !QFile::remove(path)) {
            if (errorOut)
                *errorOut = QStringLiteral("could not remove %1").arg(path);
            return false;
        }
        return true;
    }

    QDir().mkpath(QDir::homePath() + QStringLiteral("/.config/autostart"));
    const QString exe = QCoreApplication::applicationFilePath();
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorOut)
            *errorOut = QStringLiteral("could not write %1").arg(path);
        return false;
    }
    QTextStream ts(&f);
    ts << "[Desktop Entry]\n"
       << "Type=Application\n"
       << "Name=Edgeline (restore panel settings)\n"
       << "Exec=" << exe << " --restore\n"
       << "X-GNOME-Autostart-Delay=4\n"
       << "X-GNOME-Autostart-enabled=true\n"
       << "NoDisplay=true\n";
    return true;
}

} // namespace xen::settings

