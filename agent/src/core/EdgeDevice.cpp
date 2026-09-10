// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/EdgeDevice.h"

#include "transport/HidEnumerator.h"

namespace xen {

EdgeDevice::EdgeDevice(QObject* parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &EdgeDevice::pollNow);
}

void EdgeDevice::startPolling(int intervalMs)
{
    pollNow();
    m_timer.start(intervalMs);
}

void EdgeDevice::pollNow()
{
    State next;
    if (auto info = HidEnumerator::findEdge()) {
        next.present    = true;
        next.accessible = info->accessible;
        next.path       = QString::fromStdString(info->path);
        next.product    = QString::fromStdString(info->product);
        next.serial     = QString::fromStdString(info->serial);
    }
    if (next != m_state) {
        m_state = next;
        emit stateChanged(m_state);
    }
}

} // namespace xen
