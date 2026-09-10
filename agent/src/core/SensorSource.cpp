// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SensorSource.h"

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimer>

#include <iterator>
#include <numeric>

namespace xen {

namespace {

// Shared, compile-once whitespace splitter (avoids rebuilding the regex each call).
const QRegularExpression& whitespace()
{
    static const QRegularExpression re(QStringLiteral("\\s+"));
    return re;
}

// Find the k10temp (or any) "Tctl"/first CPU temp input under hwmon.
QString findCpuTempInput()
{
    const QDir base(QStringLiteral("/sys/class/hwmon"));
    for (const QString& d : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString dir = base.filePath(d);
        QFile nf(dir + QStringLiteral("/name"));
        if (!nf.open(QIODevice::ReadOnly))
            continue;
        const QString name = QString::fromUtf8(nf.readAll()).trimmed();
        if (name != QStringLiteral("k10temp") && name != QStringLiteral("coretemp"))
            continue;
        // Prefer a label of Tctl/Tdie/Package; else temp1_input.
        for (int i = 1; i <= 8; ++i) {
            QFile lf(dir + QStringLiteral("/temp%1_label").arg(i));
            QString label;
            if (lf.open(QIODevice::ReadOnly))
                label = QString::fromUtf8(lf.readAll()).trimmed();
            const QString input = dir + QStringLiteral("/temp%1_input").arg(i);
            if (!QFile::exists(input))
                continue;
            if (label.contains(QStringLiteral("Tctl")) || label.contains(QStringLiteral("Tdie"))
                || label.contains(QStringLiteral("Package")) || i == 1)
                return input;
        }
    }
    return {};
}

} // namespace

SensorSource::SensorSource(QObject* parent)
    : QObject(parent)
    , m_cpuTempPath(findCpuTempInput())
{
    m_gpu.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_units, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            &SensorSource::onUnitsFinished);
    connect(&m_gpu, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            &SensorSource::onGpuFinished);
}

void SensorSource::start(int intervalMs)
{
    if (!m_timer) {
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &SensorSource::poll);
    }
    poll();
    m_timer->start(intervalMs);
}

void SensorSource::stop()
{
    if (m_timer)
        m_timer->stop();
}

double SensorSource::readCpuLoad()
{
    QFile f(QStringLiteral("/proc/stat"));
    if (!f.open(QIODevice::ReadOnly))
        return -1;
    const QString line = QString::fromUtf8(f.readLine());
    const QStringList p = line.split(whitespace(), Qt::SkipEmptyParts);
    if (p.size() < 8 || p[0] != QStringLiteral("cpu"))
        return -1;
    unsigned long long vals[7];
    for (int i = 0; i < 7; ++i)
        vals[i] = p[i + 1].toULongLong();
    const unsigned long long idle = vals[3] + vals[4]; // idle + iowait
    const unsigned long long total = std::accumulate(std::begin(vals), std::end(vals), 0ULL);

    double load = -1;
    if (m_haveStat) {
        const unsigned long long dt = total - m_lastTotal;
        const unsigned long long di = idle - m_lastIdle;
        if (dt > 0)
            load = 100.0 * (double(dt - di) / double(dt));
    }
    m_lastIdle = idle;
    m_lastTotal = total;
    m_haveStat = true;
    return load;
}

double SensorSource::readCpuTemp()
{
    if (m_cpuTempPath.isEmpty())
        return -1;
    QFile f(m_cpuTempPath);
    if (!f.open(QIODevice::ReadOnly))
        return -1;
    bool ok = false;
    const double milli = QString::fromUtf8(f.readAll()).trimmed().toDouble(&ok);
    return ok ? milli / 1000.0 : -1;
}

void SensorSource::readMemory(SensorSnapshot& s)
{
    QFile f(QStringLiteral("/proc/meminfo"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    unsigned long long total = 0;
    unsigned long long avail = 0;
    QTextStream ts(&f);
    QString line;
    while (ts.readLineInto(&line)) {
        if (line.startsWith(QStringLiteral("MemTotal:")))
            total = line.section(whitespace(), 1, 1).toULongLong();
        else if (line.startsWith(QStringLiteral("MemAvailable:")))
            avail = line.section(whitespace(), 1, 1).toULongLong();
    }
    if (total > 0) {
        s.ramTotalGiB = static_cast<double>(total) / 1048576.0; // kB -> GiB
        s.ramUsedGiB = static_cast<double>(total - avail) / 1048576.0;
        s.ramPct = 100.0 * (static_cast<double>(total - avail) / static_cast<double>(total));
    }
}

void SensorSource::kickGpuQuery()
{
    if (m_gpu.state() != QProcess::NotRunning)
        return;
    m_gpu.start(QStringLiteral("nvidia-smi"),
                { QStringLiteral("--query-gpu=name,temperature.gpu,utilization.gpu,memory.used,memory.total"),
                  QStringLiteral("--format=csv,noheader,nounits") });
}

void SensorSource::onGpuFinished(int exitCode, QProcess::ExitStatus)
{
    if (exitCode != 0)
        return;
    const QString out = QString::fromUtf8(m_gpu.readAllStandardOutput()).trimmed();
    const QString first = out.split('\n').value(0);
    const QStringList f = first.split(',');
    if (f.size() < 5)
        return;
    m_snap.gpuName = f[0].trimmed();
    m_snap.gpuTempC = f[1].trimmed().toDouble();
    m_snap.gpuUtilPct = f[2].trimmed().toDouble();
    m_snap.gpuMemUsedGiB = f[3].trimmed().toDouble() / 1024.0;  // MiB -> GiB
    m_snap.gpuMemTotalGiB = f[4].trimmed().toDouble() / 1024.0;
    m_snap.gpuOk = true;
}

namespace {

// Interfaces whose traffic is a copy of something else's: bridges, container
// veths, tunnels and loopback. Counting them would show a machine doing twice
// the network it is doing.
bool isVirtualInterface(const QString& name)
{
    static const QStringList prefixes{
        QStringLiteral("lo"),    QStringLiteral("docker"), QStringLiteral("br-"),
        QStringLiteral("virbr"), QStringLiteral("veth"),   QStringLiteral("tun"),
        QStringLiteral("tap"),   QStringLiteral("vmnet"),  QStringLiteral("wg"),
    };
    for (const QString& p : prefixes)
        if (name.startsWith(p))
            return true;
    return false;
}

// Whole devices only. Counting nvme0n1 and its partitions together would
// double every byte.
bool isWholeDisk(const QString& name)
{
    static const QRegularExpression re(
        QStringLiteral("^(nvme\\d+n\\d+|sd[a-z]+|vd[a-z]+|mmcblk\\d+)$"));
    return re.match(name).hasMatch();
}

} // namespace

void SensorSource::readNetwork(SensorSnapshot& s)
{
    QFile f(QStringLiteral("/proc/net/dev"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double dt = m_netLastMs > 0 ? double(now - m_netLastMs) / 1000.0 : 0;
    m_netLastMs = now;

    QString bestName;
    quint64 bestRx = 0, bestTx = 0, bestTotal = 0;
    for (const QString& line : lines) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString name = line.left(colon).trimmed();
        if (isVirtualInterface(name))
            continue;
        const QStringList f2 = line.mid(colon + 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (f2.size() < 9)
            continue;
        const quint64 rx = f2.at(0).toULongLong();
        const quint64 tx = f2.at(8).toULongLong();
        if (rx + tx > bestTotal) {
            bestTotal = rx + tx;
            bestName = name;
            bestRx = rx;
            bestTx = tx;
        }
    }
    if (bestName.isEmpty())
        return;

    // A different interface, or the first sample, has no delta to report.
    if (dt > 0 && bestName == m_netName && bestRx >= m_netRx && bestTx >= m_netTx) {
        s.netRxMBs = double(bestRx - m_netRx) / dt / (1024.0 * 1024.0);
        s.netTxMBs = double(bestTx - m_netTx) / dt / (1024.0 * 1024.0);
    }
    s.netInterface = bestName;
    m_netName = bestName;
    m_netRx = bestRx;
    m_netTx = bestTx;
}

void SensorSource::readDisk(SensorSnapshot& s)
{
    QFile f(QStringLiteral("/proc/diskstats"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double dt = m_diskLastMs > 0 ? double(now - m_diskLastMs) / 1000.0 : 0;
    m_diskLastMs = now;

    QString bestName;
    quint64 bestRead = 0, bestWrite = 0, bestTotal = 0;
    for (const QString& line : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
        const QStringList f2 = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (f2.size() < 10)
            continue;
        const QString name = f2.at(2);
        if (!isWholeDisk(name))
            continue;
        // Fields 6 and 10 are sectors read and written, 512 bytes each.
        const quint64 rd = f2.at(5).toULongLong();
        const quint64 wr = f2.at(9).toULongLong();
        if (rd + wr > bestTotal) {
            bestTotal = rd + wr;
            bestName = name;
            bestRead = rd;
            bestWrite = wr;
        }
    }
    if (bestName.isEmpty())
        return;

    if (dt > 0 && bestName == m_diskName && bestRead >= m_diskRead && bestWrite >= m_diskWrite) {
        s.diskReadMBs = double(bestRead - m_diskRead) * 512.0 / dt / (1024.0 * 1024.0);
        s.diskWriteMBs = double(bestWrite - m_diskWrite) * 512.0 / dt / (1024.0 * 1024.0);
    }
    s.diskDevice = bestName;
    m_diskName = bestName;
    m_diskRead = bestRead;
    m_diskWrite = bestWrite;
}

void SensorSource::kickUnitsQuery()
{
    if (m_units.state() != QProcess::NotRunning)
        return;
    m_units.start(QStringLiteral("systemctl"),
                  { QStringLiteral("--failed"), QStringLiteral("--no-legend"),
                    QStringLiteral("--plain"), QStringLiteral("--no-pager") });
}

void SensorSource::onUnitsFinished(int exitCode, QProcess::ExitStatus)
{
    const QString out = QString::fromUtf8(m_units.readAllStandardOutput());
    if (exitCode != 0 && out.isEmpty())
        return;   // keep the last known figure rather than claiming zero
    QStringList names;
    for (const QString& line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString unit = line.trimmed().section(QLatin1Char(' '), 0, 0);
        if (!unit.isEmpty())
            names << unit;
    }
    m_snap.failedUnitNames = names;
    m_snap.failedUnits = int(names.size());
}

void SensorSource::poll()
{
    m_snap.cpuLoadPct = readCpuLoad();
    m_snap.cpuTempC = readCpuTemp();
    readMemory(m_snap);
    readNetwork(m_snap);
    readDisk(m_snap);
    kickGpuQuery();   // async; result folds into the next emit
    kickUnitsQuery(); // likewise
    emit updated(m_snap);
}

} // namespace xen
