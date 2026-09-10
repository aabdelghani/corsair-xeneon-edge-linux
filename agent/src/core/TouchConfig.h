// edgeline: gesture bindings and the lock zone.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>

namespace xen::touchcfg {

struct LockZone {
    bool enabled = false;
    // Which edge the reserved band sits against, and how much of the panel it
    // takes. A band is described rather than a rectangle because the panel is a
    // strip: what people want is "ignore the end my palm rests on".
    QString side = QStringLiteral("right");   // left | right | top | bottom
    double fraction = 0.4;                    // 0.05 .. 0.9
};

struct Config {
    bool gesturesEnabled = false;
    // gesture name -> action id. Anything unbound, or bound to "none", does
    // nothing.
    QMap<QString, QString> bindings;
    LockZone lockZone;
};

QString path();
Config load();
bool save(const Config& cfg, QString* errorOut = nullptr);

QJsonObject toJson(const Config& cfg);
Config fromJson(const QJsonObject& obj);

// The actions a gesture can be bound to, and a one-line description each.
QMap<QString, QString> actions();
bool isKnownAction(const QString& id);

// True when the point falls inside the reserved band.
bool inLockZone(const LockZone& z, double nx, double ny);

} // namespace xen::touchcfg
