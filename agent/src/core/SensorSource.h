// edgeline: system sensor readings for the Edge dashboard (M6).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Polls CPU load and temperature, RAM usage, and (via nvidia-smi) GPU stats
// once a second and emits a snapshot. Everything reads local /proc and /sys or
// shells out to nvidia-smi; no root, no daemon.
#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class QTimer;

namespace xen {

struct SensorSnapshot {
    // CPU
    double cpuLoadPct = -1;   // 0..100, -1 if unknown
    double cpuTempC = -1;
    // Memory
    double ramUsedGiB = 0;
    double ramTotalGiB = 0;
    double ramPct = -1;
    // GPU (nvidia-smi)
    double gpuUtilPct = -1;
    double gpuTempC = -1;
    double gpuMemUsedGiB = 0;
    double gpuMemTotalGiB = 0;
    QString gpuName;
    bool gpuOk = false;

    // Network: the busiest non-loopback, non-virtual interface. Picking one
    // keeps the tile honest; summing bridges and veths would double-count
    // every container's traffic.
    QString netInterface;
    double netRxMBs = -1;
    double netTxMBs = -1;

    // Disk: whole-device throughput, virtual and loop devices excluded.
    QString diskDevice;
    double diskReadMBs = -1;
    double diskWriteMBs = -1;

    // systemd units in a failed state, for the tile that reports them.
    int failedUnits = -1;
    QStringList failedUnitNames;
};

class SensorSource : public QObject {
    Q_OBJECT
public:
    explicit SensorSource(QObject* parent = nullptr);

    void start(int intervalMs = 1000);
    void stop();
    [[nodiscard]] SensorSnapshot latest() const { return m_snap; }

signals:
    void updated(const xen::SensorSnapshot& snap);

private:
    void poll();
    double readCpuLoad();      // delta since last call
    double readCpuTemp();
    static void readMemory(SensorSnapshot& s);
    void kickGpuQuery();
    void readNetwork(SensorSnapshot& s);
    void readDisk(SensorSnapshot& s);
    void kickUnitsQuery();
    void onUnitsFinished(int exitCode, QProcess::ExitStatus);
    void onGpuFinished(int exitCode, QProcess::ExitStatus);

    QTimer* m_timer = nullptr;
    QProcess m_gpu;
    QProcess m_units;

    // Counter baselines for the per-second rates.
    QString m_netName;
    quint64 m_netRx = 0;
    quint64 m_netTx = 0;
    qint64 m_netLastMs = 0;
    QString m_diskName;
    quint64 m_diskRead = 0;
    quint64 m_diskWrite = 0;
    qint64 m_diskLastMs = 0;
    SensorSnapshot m_snap;

    // /proc/stat deltas
    unsigned long long m_lastIdle = 0;
    unsigned long long m_lastTotal = 0;
    bool m_haveStat = false;
    QString m_cpuTempPath; // resolved k10temp input
};

} // namespace xen
