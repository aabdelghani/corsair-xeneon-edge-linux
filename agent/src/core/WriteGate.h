// edgeline: the single choke point for every HID write to the device.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Nothing in the app writes to the Edge except through WriteGate::exchange().
// It refuses to send unless the caller passes an explicit Confirmation token
// (which the UI only creates after the user approves the exact bytes), and it
// appends every TX and RX to ~/.local/share/edgeline/hid.log.
#pragma once

#include "proto/BragiFrame.h"

#include <QString>
#include <cstdint>
#include <utility>
#include <vector>

namespace xen {

// A capability object: only code that has shown the user the exact bytes and
// received approval can construct one. Passing it to exchange() authorizes
// exactly that one transfer.
class Confirmation {
public:
    static Confirmation approve(const QString& humanReason) { return Confirmation(humanReason); }
    [[nodiscard]] const QString& reason() const { return m_reason; }
private:
    explicit Confirmation(QString  r) : m_reason(std::move(r)) {}
    QString m_reason;
};

struct Exchange {
    bool ok = false;
    std::vector<uint8_t> tx;  // exactly what was written (64 bytes)
    std::vector<uint8_t> rx;  // what came back (empty on timeout)
    QString error;
    QString logPath;
};

class WriteGate {
public:
    // Opens `hidrawPath`, logs+writes `frame`, reads one report back, logs it.
    // Returns without sending if `conf` reason is empty.
    static Exchange exchange(const QString& hidrawPath, const BragiFrame& frame,
                             const Confirmation& conf, int readTimeoutMs = 150);

    static QString logFilePath();

private:
    static void append(const QString& line);
};

} // namespace xen
