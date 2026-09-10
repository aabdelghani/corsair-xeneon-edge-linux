// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/RulesStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>

namespace xen::rules {

QString path()
{
    return QFileInfo(QSettings().fileName()).absolutePath() + QStringLiteral("/rules.json");
}

QJsonObject toJson(const Config& cfg)
{
    QJsonArray arr;
    for (const AppRule& r : cfg.rules) {
        arr.append(QJsonObject{
            { QStringLiteral("app"), QString::fromStdString(r.app) },
            { QStringLiteral("pattern"), QString::fromStdString(r.pattern) },
            { QStringLiteral("matchOn"), QLatin1String(matchOnName(r.matchOn)) },
            { QStringLiteral("profile"), QString::fromStdString(r.profile) },
            { QStringLiteral("enabled"), r.enabled },
        });
    }
    return QJsonObject{
        { QStringLiteral("enabled"), cfg.enabled },
        { QStringLiteral("restoreOnUnfocus"), cfg.restoreOnUnfocus },
        { QStringLiteral("fallbackProfile"), cfg.fallbackProfile },
        { QStringLiteral("rules"), arr },
    };
}

Config fromJson(const QJsonObject& obj)
{
    Config cfg;
    // Rules stay off unless the file says otherwise. A config that fails to
    // parse must not silently start moving the panel around.
    cfg.enabled = obj.value(QStringLiteral("enabled")).toBool(false);
    cfg.restoreOnUnfocus = obj.value(QStringLiteral("restoreOnUnfocus")).toBool(true);
    cfg.fallbackProfile = obj.value(QStringLiteral("fallbackProfile")).toString();
    for (const QJsonValue& v : obj.value(QStringLiteral("rules")).toArray()) {
        const QJsonObject o = v.toObject();
        AppRule r;
        r.app = o.value(QStringLiteral("app")).toString().toStdString();
        r.pattern = o.value(QStringLiteral("pattern")).toString().toStdString();
        r.matchOn = matchOnFromName(o.value(QStringLiteral("matchOn")).toString().toStdString());
        r.profile = o.value(QStringLiteral("profile")).toString().toStdString();
        r.enabled = o.value(QStringLiteral("enabled")).toBool(true);
        if (!r.pattern.empty())
            cfg.rules.push_back(std::move(r));
    }
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

} // namespace xen::rules
