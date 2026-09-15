// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Gpus.h"

namespace xen {
namespace {

// nvidia-smi prints "[N/A]" for a figure the card does not report, which is
// not zero and must not be shown as zero.
double num(const QString& field)
{
    const QString s = field.trimmed();
    if (s.isEmpty() || s.startsWith(QLatin1Char('[')))
        return -1;
    bool ok = false;
    const double v = s.toDouble(&ok);
    return ok ? v : -1;
}

} // namespace

QList<GpuInfo> parseNvidiaSmi(const QString& out)
{
    QList<GpuInfo> list;
    for (const QString& line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList f = line.split(QLatin1Char(','));
        // uuid, name, then five numbers. A card name with a comma in it would
        // split into more fields, so the name is whatever sits between the
        // uuid and the last five columns rather than field 1 alone.
        if (f.size() < 7)
            continue;
        const QString uuid = f.first().trimmed();
        if (uuid.isEmpty())
            continue;

        GpuInfo g;
        g.id = QStringLiteral("nvidia:") + uuid;
        g.name = f.mid(1, f.size() - 6).join(QLatin1Char(',')).trimmed();
        g.source = QStringLiteral("nvidia-smi");
        g.tempC = num(f.at(f.size() - 5));
        g.utilPct = num(f.at(f.size() - 4));
        const double usedMiB = num(f.at(f.size() - 3));
        const double totalMiB = num(f.at(f.size() - 2));
        g.memUsedGiB = usedMiB >= 0 ? usedMiB / 1024.0 : -1;
        g.memTotalGiB = totalMiB >= 0 ? totalMiB / 1024.0 : -1;
        g.powerW = num(f.last());
        list.append(g);
    }
    return list;
}

GpuInfo fromAmdSample(const AmdGpuSample& s, const QString& name)
{
    GpuInfo g;
    // The bus address is the stable identifier. "card0" is a kernel
    // enumeration order, and adding a card can renumber it.
    g.id = QStringLiteral("amdgpu:") + (s.busAddress.isEmpty() ? s.card : s.busAddress);
    g.name = name;
    g.source = QStringLiteral("amdgpu");
    g.utilPct = s.busyPct;
    g.tempC = s.tempC;
    g.memUsedGiB = s.vramUsedGiB;
    g.memTotalGiB = s.vramTotalGiB;
    g.powerW = s.powerW;
    return g;
}

QList<GpuInfo> selectGpus(const QList<GpuInfo>& all, const QStringList& wanted)
{
    if (all.isEmpty())
        return {};

    if (!wanted.isEmpty()) {
        QList<GpuInfo> out;
        // Walking the detected list rather than the wanted list keeps the
        // order the machine reports and quietly handles an id listed twice.
        for (const GpuInfo& g : all)
            if (wanted.contains(g.id))
                out.append(g);
        if (!out.isEmpty())
            return out;
    }

    const GpuInfo* best = nullptr;
    for (const GpuInfo& g : all)
        if (!best || g.memTotalGiB > best->memTotalGiB)
            best = &g;
    return { *best };
}

} // namespace xen
