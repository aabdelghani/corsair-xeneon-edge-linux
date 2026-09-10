// edgeline: a blocking client for the agent socket.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Used by the CLI so that `edgeline set brightness 40` goes through the same
// agent the UI does. Two processes both driving ddcutil at once would
// interleave writes on one i2c bus, which is exactly the failure the agent's
// job queue exists to prevent.
#pragma once

#include <QJsonObject>
#include <QString>

namespace xen {

class RpcClient {
public:
    struct Reply {
        bool ok = false;
        QJsonObject result;
        QString error;
    };

    // Blocking. `timeoutMs` covers connect and the round trip.
    static Reply call(const QString& method, const QJsonObject& params = {},
                      int timeoutMs = 10000, const QString& socketPath = QString());

    // True when an agent is listening. Used to give a useful message rather
    // than a connection error.
    static bool agentRunning(const QString& socketPath = QString());
};

} // namespace xen
