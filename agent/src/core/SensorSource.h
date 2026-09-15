// edgeline: system sensor readings for the Edge dashboard (M6).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Polls CPU load and temperature, RAM usage and GPU stats once a second and
// emits a snapshot. Everything reads local /proc and /sys or shells out to
// nvidia-smi; no root, no daemon.
#pragma once

#include "core/Gpus.h"

#include <QHash>
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
    // GPU. Both vendors are read every poll, so a machine with a discrete
    // NVIDIA card and an integrated Radeon reports both rather than whichever
    // one answered first.
    QList<GpuInfo> gpus;       // everything detected, in the order found
    QStringList gpuIdsShown;   // the subset the dashboard is asked to draw

    // The first shown GPU, flattened. Kept because the panel tile, the preview
    // and the profile summary all read these, and because a single-GPU machine
    // has no reason to deal with a list.
    double gpuUtilPct = -1;
    double gpuTempC = -1;
    double gpuMemUsedGiB = 0;
    double gpuMemTotalGiB = 0;
    QString gpuName;
    bool gpuOk = false;
    QString gpuSource;     // "nvidia-smi" or "amdgpu"; empty when neither works
    double gpuPowerW = -1;

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

    // Which GPUs the dashboard shows. Empty means automatic: the card with the
    // most VRAM. Ids come from GpuInfo::id and are stable across reboots.
    void setGpuSelection(const QStringList& ids);
    [[nodiscard]] QStringList gpuSelection() const { return m_gpuSelection; }

    // A one-shot enumeration for the settings UI, which needs the list before
    // anything has asked for a sensor stream. Blocks on nvidia-smi for up to
    // two seconds, so it is for a click, not for the poll loop.
    QList<GpuInfo> enumerateGpus();

signals:
    void updated(const xen::SensorSnapshot& snap);

private:
    void poll();
    double readCpuLoad();      // delta since last call
    double readCpuTemp();
    static void readMemory(SensorSnapshot& s);
    void kickGpuQuery();
    void onGpuError(QProcess::ProcessError);
    QList<GpuInfo> readAmdGpuList();
    // Merges the two vendors, applies the selection and fills the flat fields.
    void applyGpus(SensorSnapshot& s);
    QString amdGpuName(const AmdGpuSample& g);
    static QStringList nvidiaQueryArgs();
    void readNetwork(SensorSnapshot& s);
    void readDisk(SensorSnapshot& s);
    void kickUnitsQuery();
    void onUnitsFinished(int exitCode, QProcess::ExitStatus);
    void onGpuFinished(int exitCode, QProcess::ExitStatus);

    QTimer* m_timer = nullptr;
    QProcess m_gpu;
    // Once nvidia-smi has failed, either because it is not installed or
    // because it exits non-zero, it is not asked again. amdgpu sysfs is read
    // either way: a failure here says nothing about the rest of the machine.
    bool m_nvidiaFailed = false;
    QList<GpuInfo> m_nvidiaGpus;          // last good nvidia-smi result
    QStringList m_gpuSelection;           // empty = automatic
    QHash<QString, QString> m_amdNames;   // bus address -> name, from lspci, once
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
