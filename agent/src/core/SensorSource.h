// edgeline: system sensor readings for the Edge dashboard (M6).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Polls CPU load and temperature, RAM usage and GPU stats once a second and
// emits a snapshot. Everything reads local /proc and /sys or shells out to
// nvidia-smi; no root, no daemon.
#pragma once

#include "core/Gpus.h"
#include "core/MprisWatcher.h"
#include "core/NotificationWatcher.h"

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

    // Network: one interface, chosen by the owner or picked automatically.
    // Picking one keeps the tile honest; summing bridges and veths would
    // double-count every container's traffic.
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

    // Host identity and uptime, for the panel header.
    QString hostName;
    qint64 uptimeSec = -1;

    // CPU topology, read once. The panel's CPU tile says "16C/32T".
    int cpuCores = -1;
    int cpuThreads = -1;

    // How full the root filesystem is, beside the throughput figures above.
    double diskUsedPct = -1;

    // Cooling and whole-package power. Plenty of machines expose neither: a
    // desktop with no hwmon fan inputs at all, and powercap energy counters
    // that are root-only since the platypus side channel was published. -1
    // therefore means "no source here", never "zero RPM" or "zero watts".
    double fanRpm = -1;
    QString fanChip;         // the hwmon chip the reading came from
    double packageWatts = -1;

    // Whatever an MPRIS player is playing. sessionBusOk separates "nothing is
    // playing" from "there is no session bus here", which the panel has to
    // tell apart to stay honest.
    NowPlaying nowPlaying;
    bool sessionBusOk = false;

    // Desktop notifications, newest first. notifyActive separates "nothing has
    // arrived yet" from "the bus would not let us watch", which are different
    // things to put on a panel.
    QList<Notification> notifications;
    bool notifyActive = false;
    QString notifyError;
};

// One row of /proc/net/dev, with the link state from sysfs.
struct NetInterface {
    QString name;
    quint64 rxBytes = 0;
    quint64 txBytes = 0;
    bool up = false;         // operstate "up", or "unknown" with a carrier
    bool wireless = false;   // has /sys/class/net/<name>/wireless
    bool isVirtual = false;  // loopback, bridge, veth, tunnel and the like
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

    // Which interface the network tile reads. Empty means automatic: the
    // busiest physical interface whose link is up.
    void setNetSelection(const QString& name) { m_netSelection = name; }
    [[nodiscard]] QString netSelection() const { return m_netSelection; }

    // Every interface the kernel lists, for the picker. Cheap: two small
    // files per interface.
    static QList<NetInterface> enumerateInterfaces();

    // Drops the notifications collected so far. The panel's CLEAR ALL dismisses
    // what this agent has seen; it does not reach into the desktop's own
    // notification history, which is not ours to edit.
    void clearNotifications() { m_notify.clear(); }

    // Previous, play/pause or next on the player the now playing tile shows.
    bool mediaControl(const QString& action, QString* error) { return m_mpris.control(action, error); }

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
    static void readUptime(SensorSnapshot& s);
    static void readDiskUsage(SensorSnapshot& s);
    static void readFan(SensorSnapshot& s);
    void readPackagePower(SensorSnapshot& s);
    void readCpuTopology(SensorSnapshot& s);   // cached after the first call
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
    MprisWatcher m_mpris;
    NotificationWatcher m_notify;

    // Counter baselines for the per-second rates.
    QString m_netName;
    QString m_netSelection;               // empty = automatic
    quint64 m_netRx = 0;
    quint64 m_netTx = 0;
    qint64 m_netLastMs = 0;
    QString m_diskName;
    quint64 m_diskRead = 0;
    quint64 m_diskWrite = 0;
    qint64 m_diskLastMs = 0;

    // Topology does not change while the process runs, so /proc/cpuinfo is
    // parsed once rather than 86400 times a day.
    int m_cpuCores = -1;
    int m_cpuThreads = -1;
    // Energy counters are cumulative microjoules; watts is their derivative.
    quint64 m_energyUj = 0;
    qint64 m_energyLastMs = 0;
    QString m_energyPath;
    bool m_energyResolved = false;

    SensorSnapshot m_snap;

    // /proc/stat deltas
    unsigned long long m_lastIdle = 0;
    unsigned long long m_lastTotal = 0;
    bool m_haveStat = false;
    QString m_cpuTempPath; // resolved k10temp input
};

} // namespace xen
