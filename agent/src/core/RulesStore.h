// edgeline: persistence for per-app rules.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/AppRules.h"

#include <QJsonObject>
#include <QString>
#include <vector>

namespace xen::rules {

struct Config {
    bool enabled = false;          // off until asked for: rules move the panel
    bool restoreOnUnfocus = true;  // the design's "Restore previous on unfocus"
    QString fallbackProfile;       // applied when nothing matches; empty = leave alone
    std::vector<AppRule> rules;
};

QString path();
Config load();
bool save(const Config& cfg, QString* errorOut = nullptr);

QJsonObject toJson(const Config& cfg);
Config fromJson(const QJsonObject& obj);

} // namespace xen::rules
