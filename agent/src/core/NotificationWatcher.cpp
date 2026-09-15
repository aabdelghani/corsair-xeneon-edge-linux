// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/NotificationWatcher.h"

#include <dbus/dbus.h>

namespace xen {
namespace {

// Only Notify calls, so the monitor is not handed every message on the bus.
const char* kRule =
    "type='method_call',interface='org.freedesktop.Notifications',member='Notify'";

// The design shows five. Keeping more would be memory spent on rows no panel
// is ever going to draw.
constexpr int kKeep = 5;

QString takeString(DBusMessageIter* it)
{
    if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_STRING)
        return {};
    const char* s = nullptr;
    dbus_message_iter_get_basic(it, &s);
    dbus_message_iter_next(it);
    return QString::fromUtf8(s ? s : "");
}

DBusHandlerResult filter(DBusConnection*, DBusMessage* msg, void* user)
{
    // Take only calls addressed to the well-known name, which is how an
    // application submits a notification.
    //
    // GNOME Shell re-issues every notification to itself, so a monitor sees
    // each one twice. Measured on this desktop, one notification produced:
    //
    //   sender=:1.375 -> destination=org.freedesktop.Notifications  serial=3
    //   sender=:1.46  -> destination=:1.40                          serial=313
    //
    // The second is the shell's own copy, carrying an x-shell-sender hint that
    // names the original caller. Sender and serial both differ, so no amount of
    // message-identity checking separates them; the destination does. Anything
    // addressed to a unique name is the daemon's internal plumbing, not a
    // notification somebody sent.
    const char* dest = dbus_message_get_destination(msg);
    const bool toDaemon = dest && qstrcmp(dest, "org.freedesktop.Notifications") == 0;

    if (toDaemon && dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_CALL
        && dbus_message_is_method_call(msg, "org.freedesktop.Notifications", "Notify")) {
        // Notify(s app_name, u replaces_id, s app_icon, s summary, s body, ...)
        DBusMessageIter it;
        if (dbus_message_iter_init(msg, &it)) {
            Notification n;
            n.app = takeString(&it);
            if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_UINT32)
                dbus_message_iter_next(&it);          // replaces_id
            takeString(&it);                          // app_icon: no icon theme here
            n.summary = takeString(&it);
            n.body = takeString(&it);
            n.when = QDateTime::currentDateTime();
            if (!n.summary.isEmpty() || !n.body.isEmpty())
                static_cast<NotificationWatcher*>(user)->record(n);
        }
    }
    // HANDLED, which took a bug to work out.
    //
    // The instinct is to return NOT_YET_HANDLED so as not to "claim" a message
    // that belongs to somebody else. That is wrong twice over. What a monitor
    // receives is a copy of a message the bus has already delivered, so this
    // return cannot keep the real recipient from seeing it. And what
    // NOT_YET_HANDLED actually means to libdbus is that nobody dealt with this
    // method call, whereupon libdbus helpfully sends an UnknownMethod error
    // back to the caller. A monitor connection is forbidden from sending
    // anything at all, so the bus drops it the instant that reply goes out.
    //
    // Symptom, before this was understood: the first notification arrived, the
    // monitor was killed, and every notification after it vanished with no
    // error anywhere.
    return DBUS_HANDLER_RESULT_HANDLED;
}

} // namespace

NotificationWatcher::~NotificationWatcher()
{
    if (m_conn) {
        dbus_connection_remove_filter(m_conn, filter, this);
        // A private connection has to be closed explicitly; unref alone leaks
        // the socket until the process exits.
        dbus_connection_close(m_conn);
        dbus_connection_unref(m_conn);
        m_conn = nullptr;
    }
}

bool NotificationWatcher::start()
{
    if (m_conn)
        return true;
    if (m_tried)
        return false;   // a refusal is policy, not a transient failure
    m_tried = true;

    DBusError err;
    dbus_error_init(&err);

    DBusConnection* conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
    if (!conn) {
        m_error = QString::fromUtf8(dbus_error_is_set(&err) ? err.message : "no session bus");
        dbus_error_free(&err);
        return false;
    }
    // The agent outlives a bus restart; it must not be killed by one.
    dbus_connection_set_exit_on_disconnect(conn, FALSE);

    DBusMessage* call = dbus_message_new_method_call(
        "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus.Monitoring", "BecomeMonitor");
    if (!call) {
        m_error = QStringLiteral("could not build BecomeMonitor");
        dbus_connection_close(conn);
        dbus_connection_unref(conn);
        dbus_error_free(&err);
        return false;
    }

    // BecomeMonitor(as rules, u flags)
    DBusMessageIter args;
    DBusMessageIter rules;
    dbus_message_iter_init_append(call, &args);
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "s", &rules);
    const char* rule = kRule;
    dbus_message_iter_append_basic(&rules, DBUS_TYPE_STRING, &rule);
    dbus_message_iter_close_container(&args, &rules);
    const dbus_uint32_t flags = 0;
    dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &flags);

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, call, 2000, &err);
    dbus_message_unref(call);
    if (!reply) {
        m_error = QString::fromUtf8(dbus_error_is_set(&err) ? err.message : "BecomeMonitor refused");
        dbus_error_free(&err);
        dbus_connection_close(conn);
        dbus_connection_unref(conn);
        return false;
    }
    dbus_message_unref(reply);
    dbus_error_free(&err);

    if (!dbus_connection_add_filter(conn, filter, this, nullptr)) {
        m_error = QStringLiteral("out of memory adding the bus filter");
        dbus_connection_close(conn);
        dbus_connection_unref(conn);
        return false;
    }

    m_conn = conn;
    return true;
}

void NotificationWatcher::pump()
{
    if (!m_conn)
        return;
    // Zero timeout: take whatever has already arrived and return. The filter
    // runs inside dispatch, so this is where recorded notifications appear.
    dbus_connection_read_write(m_conn, 0);
    while (dbus_connection_dispatch(m_conn) == DBUS_DISPATCH_DATA_REMAINS) { }

    // A dropped monitor is completely silent: messages simply stop arriving,
    // and the last few notifications sit on the panel looking current. Notice
    // it and say so, rather than showing stale rows forever.
    if (!dbus_connection_get_is_connected(m_conn)) {
        dbus_connection_remove_filter(m_conn, filter, this);
        dbus_connection_close(m_conn);
        dbus_connection_unref(m_conn);
        m_conn = nullptr;
        m_error = QStringLiteral("the bus dropped the monitor connection");
    }
}

void NotificationWatcher::record(const Notification& n)
{
    // No duplicate check here on purpose. Deduplicating by message identity
    // (sender plus serial) was tried and was wrong: every short-lived client
    // gets a fresh connection whose Notify lands on the same serial, so two
    // unrelated notifications collide on one key and the second is lost. The
    // destination check in the filter is what removes the real duplicate, and
    // one mechanism that is correct beats two that overlap.
    m_recent.prepend(n);
    while (m_recent.size() > kKeep)
        m_recent.removeLast();
}

} // namespace xen
