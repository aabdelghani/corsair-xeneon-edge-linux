// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AmdGpu.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace xen {
namespace {

// First line of a sysfs attribute, or empty. Absence is normal here: not every
// card exposes every attribute, and the caller decides what is required.
QString readAttr(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readLine()).trimmed();
}

double readNumber(const QString& path, double scale = 1.0)
{
    const QString s = readAttr(path);
    if (s.isEmpty())
        return -1;
    bool ok = false;
    const double v = s.toDouble(&ok);
    return ok ? v * scale : -1;
}

// The hwmon directory that belongs to this card. It is under the card's own
// device directory, so there is no need to search the global hwmon list and
// guess which sensor is which.
QString findHwmon(const QString& deviceDir)
{
    const QDir hw(deviceDir + QStringLiteral("/hwmon"));
    for (const QString& name : hw.entryList({ QStringLiteral("hwmon*") }, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString dir = hw.filePath(name);
        if (readAttr(dir + QStringLiteral("/name")) == QLatin1String("amdgpu"))
            return dir;
    }
    return {};
}

} // namespace

QList<AmdGpuSample> readAmdGpus(const QString& drmRoot)
{
    QList<AmdGpuSample> out;
    static const QRegularExpression cardName(QStringLiteral("^card\\d+$"));
    static const QRegularExpression busAddr(QStringLiteral("^[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\\.[0-9]$"));

    const QDir root(drmRoot);
    for (const QString& name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!cardName.match(name).hasMatch())
            continue;   // card2-DP-5 and friends are connectors, not cards
        const QString dev = root.filePath(name) + QStringLiteral("/device");
        if (readAttr(dev + QStringLiteral("/vendor")).toLower() != QLatin1String("0x1002"))
            continue;

        AmdGpuSample s;
        s.busyPct = readNumber(dev + QStringLiteral("/gpu_busy_percent"));
        s.vramUsedGiB = readNumber(dev + QStringLiteral("/mem_info_vram_used"), 1.0 / (1024.0 * 1024.0 * 1024.0));
        s.vramTotalGiB = readNumber(dev + QStringLiteral("/mem_info_vram_total"), 1.0 / (1024.0 * 1024.0 * 1024.0));
        // A card with neither a load figure nor a VRAM figure is not one the
        // amdgpu driver is running; it has nothing worth showing.
        if (s.busyPct < 0 && s.vramTotalGiB < 0)
            continue;

        s.card = name;
        s.deviceId = readAttr(dev + QStringLiteral("/device")).toLower();
        if (s.deviceId.startsWith(QLatin1String("0x")))
            s.deviceId.remove(0, 2);
        const QString resolved = QFileInfo(dev).canonicalFilePath();
        const QString base = QFileInfo(resolved).fileName();
        if (busAddr.match(base).hasMatch())
            s.busAddress = base;

        const QString hw = findHwmon(dev);
        if (!hw.isEmpty()) {
            s.tempC = readNumber(hw + QStringLiteral("/temp1_input"), 1.0 / 1000.0);   // millidegrees
            // Newer kernels report power1_average; older ones power1_input. Both
            // are microwatts.
            s.powerW = readNumber(hw + QStringLiteral("/power1_average"), 1.0 / 1e6);
            if (s.powerW < 0)
                s.powerW = readNumber(hw + QStringLiteral("/power1_input"), 1.0 / 1e6);
        }
        out.append(s);
    }
    return out;
}

const AmdGpuSample* pickAmdGpu(const QList<AmdGpuSample>& cards)
{
    const AmdGpuSample* best = nullptr;
    for (const AmdGpuSample& c : cards)
        if (!best || c.vramTotalGiB > best->vramTotalGiB)
            best = &c;
    return best;
}

} // namespace xen
