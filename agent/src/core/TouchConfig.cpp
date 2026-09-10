// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/TouchConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>

#include <algorithm>

namespace xen::touchcfg {

QString path()
{
    return QFileInfo(QSettings().fileName()).absolutePath() + QStringLiteral("/touch.json");
}

QMap<QString, QString> actions()
{
    return {
        { QStringLiteral("none"), QStringLiteral("do nothing") },
        { QStringLiteral("profile-next"), QStringLiteral("next profile") },
        { QStringLiteral("profile-previous"), QStringLiteral("previous profile") },
        { QStringLiteral("blank-toggle"), QStringLiteral("blank or unblank the panel") },
        { QStringLiteral("brightness-up"), QStringLiteral("brightness up 10") },
        { QStringLiteral("brightness-down"), QStringLiteral("brightness down 10") },
        { QStringLiteral("dashboard-toggle"), QStringLiteral("show or hide the dashboard") },
        { QStringLiteral("dashboard-page-next"), QStringLiteral("next dashboard page") },
    };
}

bool isKnownAction(const QString& id) { return actions().contains(id); }

bool inLockZone(const LockZone& z, double nx, double ny)
{
    if (!z.enabled)
        return false;
    const double f = std::clamp(z.fraction, 0.05, 0.9);
    if (z.side == QLatin1String("left"))   return nx <= f;
    if (z.side == QLatin1String("right"))  return nx >= 1.0 - f;
    if (z.side == QLatin1String("top"))    return ny <= f;
    if (z.side == QLatin1String("bottom")) return ny >= 1.0 - f;
    return false;
}

QJsonObject toJson(const Config& cfg)
{
    QJsonObject binds;
    for (auto it = cfg.bindings.constBegin(); it != cfg.bindings.constEnd(); ++it)
        binds.insert(it.key(), it.value());
    return QJsonObject{
        { QStringLiteral("gestures"),
          QJsonObject{ { QStringLiteral("enabled"), cfg.gesturesEnabled },
                       { QStringLiteral("bindings"), binds } } },
        { QStringLiteral("lockZone"),
          QJsonObject{ { QStringLiteral("enabled"), cfg.lockZone.enabled },
                       { QStringLiteral("side"), cfg.lockZone.side },
                       { QStringLiteral("fraction"), cfg.lockZone.fraction } } },
    };
}

Config fromJson(const QJsonObject& obj)
{
    Config cfg;
    const QJsonObject g = obj.value(QStringLiteral("gestures")).toObject();
    // Off unless the file says otherwise: a config that fails to parse must not
    // start changing the panel when someone brushes the glass.
    cfg.gesturesEnabled = g.value(QStringLiteral("enabled")).toBool(false);
    const QJsonObject binds = g.value(QStringLiteral("bindings")).toObject();
    for (auto it = binds.constBegin(); it != binds.constEnd(); ++it) {
        const QString action = it.value().toString();
        if (isKnownAction(action))
            cfg.bindings.insert(it.key(), action);
    }

    const QJsonObject z = obj.value(QStringLiteral("lockZone")).toObject();
    cfg.lockZone.enabled = z.value(QStringLiteral("enabled")).toBool(false);
    const QString side = z.value(QStringLiteral("side")).toString(QStringLiteral("right"));
    cfg.lockZone.side = (side == QLatin1String("left") || side == QLatin1String("right")
                         || side == QLatin1String("top") || side == QLatin1String("bottom"))
        ? side : QStringLiteral("right");
    cfg.lockZone.fraction = std::clamp(z.value(QStringLiteral("fraction")).toDouble(0.4), 0.05, 0.9);
    return cfg;
}

Config load()
{
    QFile f(path());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return fromJson(QJsonDocument::fromJson(f.readAll()).object());
}

bool save(const Config& cfg, QString* errorOut)
{
    QDir().mkpath(QFileInfo(path()).absolutePath());
    QSaveFile f(path());
    if (!f.open(QIODevice::WriteOnly)) {
        if (errorOut) *errorOut = QStringLiteral("could not write %1").arg(path());
        return false;
    }
    f.write(QJsonDocument(toJson(cfg)).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (errorOut) *errorOut = QStringLiteral("could not save %1").arg(path());
        return false;
    }
    return true;
}

} // namespace xen::touchcfg
