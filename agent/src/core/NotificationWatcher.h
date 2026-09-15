// edgeline: desktop notifications, as they are delivered to whoever shows them.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A notification is a method call to org.freedesktop.Notifications, not a
// signal. You cannot subscribe to it: only a bus monitor sees method calls
// addressed to somebody else, which is what org.freedesktop.DBus.Monitoring
// exists for.
//
// That is also why this one class uses libdbus directly instead of QtDBus.
// QtDBus routes incoming messages to registered objects and subscribed
// signals, and offers no message filter to hang a monitor on, so a monitored
// method call would arrive and be dropped.
//
// The connection is private and monitor only. Once BecomeMonitor succeeds the
// bus refuses to let that connection send anything at all, so this cannot act
// on the user's behalf even by mistake: it can only watch.
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

struct DBusConnection;

namespace xen {

struct Notification {
    QString app;       // app_name as the sender gave it
    QString summary;   // the bold line
    QString body;
    QDateTime when;
};

class NotificationWatcher {
public:
    NotificationWatcher() = default;
    ~NotificationWatcher();
    NotificationWatcher(const NotificationWatcher&) = delete;
    NotificationWatcher& operator=(const NotificationWatcher&) = delete;

    // Idempotent. False when there is no session bus, or when the bus refuses
    // to make us a monitor, which is a policy decision on some systems and not
    // something to retry in a loop.
    bool start();

    // Non-blocking. Call it from the existing poll; notifications are rare
    // enough that a second of latency on a wall panel is not worth a thread.
    void pump();

    [[nodiscard]] bool active() const { return m_conn != nullptr; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] QList<Notification> recent() const { return m_recent; }
    void clear() { m_recent.clear(); }

    // Called by the bus filter, which is a free function in the .cpp because
    // libdbus takes a C callback. Not for general use.
    void record(const Notification& n);

private:
    DBusConnection* m_conn = nullptr;
    QString m_error;
    QList<Notification> m_recent;   // newest first
    bool m_tried = false;
};

} // namespace xen
