// SPDX-License-Identifier: GPL-3.0-or-later
#include "ipc/RpcClient.h"

#include "ipc/RpcServer.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLocalSocket>

namespace xen {

bool RpcClient::agentRunning(const QString& socketPath)
{
    QLocalSocket s;
    s.connectToServer(socketPath.isEmpty() ? RpcServer::defaultSocketPath() : socketPath);
    const bool up = s.waitForConnected(500);
    if (up)
        s.disconnectFromServer();
    return up;
}

RpcClient::Reply RpcClient::call(const QString& method, const QJsonObject& params,
                                 int timeoutMs, const QString& socketPath)
{
    Reply rep;
    const QString path = socketPath.isEmpty() ? RpcServer::defaultSocketPath() : socketPath;

    QLocalSocket sock;
    sock.connectToServer(path);
    if (!sock.waitForConnected(qMin(timeoutMs, 2000))) {
        rep.error = QStringLiteral("no agent listening on %1 (start edgeline-agent)").arg(path);
        return rep;
    }

    QJsonObject req{ { QStringLiteral("id"), 1 }, { QStringLiteral("method"), method } };
    if (!params.isEmpty())
        req.insert(QStringLiteral("params"), params);
    sock.write(QJsonDocument(req).toJson(QJsonDocument::Compact));
    sock.write("\n");
    if (!sock.waitForBytesWritten(2000)) {
        rep.error = QStringLiteral("could not send the request");
        return rep;
    }

    // Read until a full line arrives. Events may be interleaved ahead of the
    // reply, so skip anything without our id.
    QByteArray buf;
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < timeoutMs) {
        if (!sock.waitForReadyRead(int(timeoutMs - clock.elapsed())))
            break;
        buf.append(sock.readAll());
        int nl = 0;
        while ((nl = buf.indexOf('\n')) >= 0) {
            const QByteArray line = buf.left(nl);
            buf.remove(0, nl + 1);
            const QJsonObject obj = QJsonDocument::fromJson(line).object();
            if (!obj.contains(QStringLiteral("id")))
                continue; // an event, not our answer
            if (obj.contains(QStringLiteral("error"))) {
                rep.error = obj.value(QStringLiteral("error")).toString();
                return rep;
            }
            rep.result = obj.value(QStringLiteral("result")).toObject();
            rep.ok = true;
            return rep;
        }
    }
    rep.error = QStringLiteral("timed out waiting for the agent");
    return rep;
}

} // namespace xen
