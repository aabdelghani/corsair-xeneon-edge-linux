// edgeline: async DDC/CI access to the Edge via ddcutil.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One ddcutil process at a time (DDC is slow and stateful); set-jobs for the
// same VCP code coalesce while queued so slider drags don't build a backlog.
#pragma once

#include <QList>
#include <QObject>
#include <QProcess>

namespace xen {

namespace vcp {
constexpr quint8 kBrightness = 0x10;
constexpr quint8 kContrast   = 0x12;
constexpr quint8 kPreset     = 0x14; // non-continuous: sRGB/native/5000K/…
constexpr quint8 kGainRed    = 0x16;
constexpr quint8 kGainGreen  = 0x18;
constexpr quint8 kGainBlue   = 0x1A;
constexpr quint8 kSharpness  = 0x87;
constexpr quint8 kPower      = 0xD6; // 0x01 on, 0x05 off (write-only off)
} // namespace vcp

class DdcClient : public QObject {
    Q_OBJECT
public:
    explicit DdcClient(QObject* parent = nullptr);

    void start(); // async: detect the Edge's i2c bus once
    [[nodiscard]] bool ready() const { return m_bus >= 0; }

    void getVcp(quint8 code);
    void setVcp(quint8 code, quint16 value);

    // Read the panel's capability string once. This is what tells us the real
    // preset list and the real input-source list, rather than assuming a set.
    // It is slow (seconds), so it is fetched once and cached by the caller.
    void fetchCapabilities();

signals:
    void readyChanged(bool ready, const QString& message);
    void vcpRead(quint8 code, quint16 current, quint16 max);
    void capabilitiesRead(const QString& raw);
    void vcpWritten(quint8 code, quint16 value);
    void errorOccurred(const QString& message);

private:
    struct Job {
        enum Kind { Detect, Get, Set, Capabilities } kind = Detect;
        quint8 code = 0;
        quint16 value = 0;
    };

    void enqueue(Job job);
    void startNext();
    void finishJob(int exitCode);

    QProcess m_proc;
    QList<Job> m_queue;
    Job m_current;
    bool m_running = false;
    int m_bus = -1;
};

} // namespace xen
