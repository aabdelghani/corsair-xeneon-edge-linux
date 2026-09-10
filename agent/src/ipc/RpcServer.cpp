// SPDX-License-Identifier: GPL-3.0-or-later
#include "ipc/RpcServer.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>

#include <unistd.h>

namespace xen {
namespace {

// One line must not be unbounded: a client that never sends a newline would
// otherwise grow the buffer until the agent is killed. No legitimate request
// comes close to this.
constexpr int kMaxLineBytes = 1 << 20; // 1 MiB

} // namespace

RpcServer::RpcServer(QObject* parent)
    : QObject(parent)
    , m_server(new QLocalServer(this))
{
    connect(m_server, &QLocalServer::newConnection, this, &RpcServer::onNewConnection);
}

RpcServer::~RpcServer() { stop(); }

QString RpcServer::defaultSocketPath()
{
    QString dir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (dir.isEmpty())
        dir = QStringLiteral("/run/user/%1").arg(getuid());
    return dir + QStringLiteral("/edgeline.sock");
}

bool RpcServer::listen(const QString& path)
{
    m_path = path.isEmpty() ? defaultSocketPath() : path;

    // A stale socket from a crashed agent would block bind. Removing it is safe
    // because the path is inside the user's own runtime dir, and because a
    // still-running agent would be holding it (checked by trying to connect).
    if (QFile::exists(m_path)) {
        QLocalSocket probe;
        probe.connectToServer(m_path);
        if (probe.waitForConnected(200)) {
            probe.disconnectFromServer();
            m_error = QStringLiteral("another edgeline agent is already listening on %1").arg(m_path);
            return false;
        }
        QLocalServer::removeServer(m_path);
    }

    QDir().mkpath(QFileInfo(m_path).absolutePath());
    if (!m_server->listen(m_path)) {
        m_error = m_server->errorString();
        return false;
    }
    // The runtime dir is already private to this user; make the socket match
    // rather than relying on the directory alone.
    QFile::setPermissions(m_path, QFile::ReadOwner | QFile::WriteOwner);
    return true;
}

void RpcServer::stop()
{
    if (!m_server)
        return;
    const auto socks = m_buffers.keys();
    for (QLocalSocket* s : socks)
        s->disconnectFromServer();
    m_buffers.clear();
    if (m_server->isListening()) {
        m_server->close();
        QLocalServer::removeServer(m_path);
    }
}

int RpcServer::clientCount() const { return int(m_buffers.size()); }

void RpcServer::addMethod(const QString& name, Handler fn)
{
    m_methods.insert(name, std::move(fn));
}

void RpcServer::onNewConnection()
{
    while (QLocalSocket* sock = m_server->nextPendingConnection()) {
        m_buffers.insert(sock, QByteArray());
        connect(sock, &QLocalSocket::readyRead, this, [this, sock]() { onReadyRead(sock); });
        connect(sock, &QLocalSocket::disconnected, this, [this, sock]() {
            m_buffers.remove(sock);
            sock->deleteLater();
            emit clientDisconnected();
        });
        emit clientConnected();
    }
}

void RpcServer::onReadyRead(QLocalSocket* sock)
{
    auto it = m_buffers.find(sock);
    if (it == m_buffers.end())
        return;
    it->append(sock->readAll());

    int nl = 0;
    while ((nl = it->indexOf('\n')) >= 0) {
        const QByteArray line = it->left(nl);
        it->remove(0, nl + 1);
        if (line.trimmed().isEmpty())
            continue;

        QJsonParseError perr{};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            sendLine(sock, QJsonObject{ { QStringLiteral("error"),
                                          QStringLiteral("malformed JSON: %1").arg(perr.errorString()) } });
            continue;
        }
        dispatch(sock, doc.object());
    }

    if (it->size() > kMaxLineBytes) {
        sendLine(sock, QJsonObject{ { QStringLiteral("error"),
                                      QStringLiteral("request line too long") } });
        it->clear();
        sock->disconnectFromServer();
    }
}

void RpcServer::dispatch(QLocalSocket* sock, const QJsonObject& msg)
{
    const QJsonValue idVal = msg.value(QStringLiteral("id"));
    const QString method = msg.value(QStringLiteral("method")).toString();

    auto reply = [&](const QJsonObject& body) {
        QJsonObject out = body;
        if (!idVal.isUndefined())
            out.insert(QStringLiteral("id"), idVal);
        sendLine(sock, out);
    };

    if (method.isEmpty()) {
        reply(QJsonObject{ { QStringLiteral("error"), QStringLiteral("missing 'method'") } });
        return;
    }
    const auto it = m_methods.constFind(method);
    if (it == m_methods.constEnd()) {
        reply(QJsonObject{ { QStringLiteral("error"),
                             QStringLiteral("unknown method '%1'").arg(method) } });
        return;
    }

    QJsonObject result;
    QString error;
    const bool ok = (*it)(msg.value(QStringLiteral("params")).toObject(), result, error);
    if (ok)
        reply(QJsonObject{ { QStringLiteral("result"), result } });
    else
        reply(QJsonObject{ { QStringLiteral("error"),
                             error.isEmpty() ? QStringLiteral("failed") : error } });
}

void RpcServer::broadcast(const QString& event, const QJsonObject& data)
{
    const QJsonObject msg{ { QStringLiteral("event"), event }, { QStringLiteral("data"), data } };
    for (auto it = m_buffers.constBegin(); it != m_buffers.constEnd(); ++it)
        sendLine(it.key(), msg);
}

void RpcServer::sendLine(QLocalSocket* sock, const QJsonObject& obj)
{
    if (!sock || sock->state() != QLocalSocket::ConnectedState)
        return;
    sock->write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    sock->write("\n");
}

} // namespace xen
