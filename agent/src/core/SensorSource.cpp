// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SensorSource.h"

#include <QDir>
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

void SensorSource::poll()
{
    m_snap.cpuLoadPct = readCpuLoad();
    m_snap.cpuTempC = readCpuTemp();
    readMemory(m_snap);
    kickGpuQuery(); // async; result folds into the next emit
    emit updated(m_snap);
}

} // namespace xen
