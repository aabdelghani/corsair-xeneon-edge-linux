// edgeline: report which window has focus.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Feeds the per-app rules. Strictly read-only: it selects property
// notifications on the root window and reads WM_CLASS and _NET_WM_STATE off
// whatever _NET_ACTIVE_WINDOW points at. It never moves, raises or changes a
// window.
//
// Its own X connection, folded into the Qt event loop with a socket notifier,
// for the same reason TouchEventSource has one: nothing else has to know it
// exists, and it keeps working when the toolkit's own input handling does not.
#pragma once

#include "core/AppRules.h"

#include <QObject>
#include <QString>

class QSocketNotifier;

namespace xen {

class FocusWatcher : public QObject {
    Q_OBJECT
public:
    explicit FocusWatcher(QObject* parent = nullptr);
    ~FocusWatcher() override;

    bool start();
    void stop();
    [[nodiscard]] bool running() const { return m_dpy != nullptr; }
    [[nodiscard]] QString lastError() const { return m_error; }

    // The window in focus right now, read on demand.
    [[nodiscard]] WindowInfo current() const { return m_current; }

signals:
    void focusChanged(const xen::WindowInfo& win);

private:
    void onReadable();
    void readActive();

    void* m_dpy = nullptr;      // Display*, kept opaque to keep Xlib out of the header
    unsigned long m_root = 0;
    QSocketNotifier* m_notifier = nullptr;
    WindowInfo m_current;
    QString m_error;
};

} // namespace xen
