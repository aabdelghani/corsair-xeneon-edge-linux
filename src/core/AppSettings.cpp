// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTextStream>

namespace xen {
namespace settings {

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
        const int code = key.mid(4).toInt(&ok, 16); // "vcp_XX"
        if (ok)
            out.insert(code, s.value(key).toInt());
    }
    s.endGroup();
    return out;
}

namespace {
QString autostartPath()
{
    return QDir::homePath() + QStringLiteral("/.config/autostart/xeneon-ctl.desktop");
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
       << "Name=Xeneon Edge (restore touch + display)\n"
       << "Exec=" << exe << " --restore\n"
       << "X-GNOME-Autostart-Delay=4\n"
       << "X-GNOME-Autostart-enabled=true\n"
       << "NoDisplay=true\n";
    return true;
}

} // namespace settings
} // namespace xen
