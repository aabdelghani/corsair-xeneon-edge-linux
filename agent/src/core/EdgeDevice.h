// edgeline: Qt facade over the Xeneon Edge device state.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

namespace xen {

class EdgeDevice : public QObject {
    Q_OBJECT
public:
    struct State {
        bool present = false;    // USB device enumerated
        bool accessible = false; // hidraw node openable by this user
        QString path;
        QString product;
        QString serial;

        bool operator==(const State& o) const
        {
            return present == o.present && accessible == o.accessible
                && path == o.path && product == o.product && serial == o.serial;
        }
        bool operator!=(const State& o) const { return !(*this == o); }
    };

    explicit EdgeDevice(QObject* parent = nullptr);

    [[nodiscard]] State state() const { return m_state; }
    void startPolling(int intervalMs = 2000);
    void pollNow();

signals:
    void stateChanged(const xen::EdgeDevice::State& state);

private:
    State m_state;
    QTimer m_timer;
};

} // namespace xen
