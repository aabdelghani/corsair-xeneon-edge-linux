// edgeline: newline-delimited JSON RPC over a UNIX socket.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The agent owns every device; the UI owns none of them and talks to the agent
// through this. Framing is one JSON object per line, which is trivial to drive
// from a shell with socat while no UI exists yet, and that property is worth
// keeping: agent bugs and UI bugs stay separable.
//
// Wire format
//   request   {"id":1,"method":"ddc.set","params":{...}}
//   response  {"id":1,"result":{...}}  |  {"id":1,"error":"..."}
//   event     {"event":"touch","data":{...}}          (server to client, unsolicited)
//
// The socket lives at $XDG_RUNTIME_DIR/edgeline.sock, which is already
// per-user and mode 0700, so no other user can reach it. There is no
// authentication beyond that and there should not be: this speaks for the
// logged-in user to their own hardware.
#pragma once

#include <QJsonObject>
#include <QLocalServer>
#include <QObject>
#include <functional>

class QLocalSocket;

namespace xen {

class RpcServer : public QObject {
    Q_OBJECT
public:
    // A handler either fills `result` and returns true, or fills `error` and
    // returns false. Long operations should instead return immediately and
    // emit an event later.
    using Handler = std::function<bool(const QJsonObject& params, QJsonObject& result, QString& error)>;

    explicit RpcServer(QObject* parent = nullptr);
    ~RpcServer() override;

    // Default path is $XDG_RUNTIME_DIR/edgeline.sock, falling back to
    // /run/user/<uid>/ when the variable is unset (cron, some display managers).
    static QString defaultSocketPath();

    bool listen(const QString& path = QString());
    void stop();
    [[nodiscard]] QString socketPath() const { return m_path; }
    [[nodiscard]] QString lastError() const { return m_error; }
    [[nodiscard]] int clientCount() const;

    void addMethod(const QString& name, Handler fn);

    // Push to every connected client. Used for touch, sensors, device
    // hotplug and DDC log lines.
    void broadcast(const QString& event, const QJsonObject& data);

signals:
    void clientConnected();
    void clientDisconnected();

private:
    void onNewConnection();
    void onReadyRead(QLocalSocket* sock);
    void dispatch(QLocalSocket* sock, const QJsonObject& msg);
    static void sendLine(QLocalSocket* sock, const QJsonObject& obj);

    QLocalServer* m_server = nullptr;
    QString m_path;
    QString m_error;
    QHash<QLocalSocket*, QByteArray> m_buffers;
    QHash<QString, Handler> m_methods;
};

} // namespace xen
