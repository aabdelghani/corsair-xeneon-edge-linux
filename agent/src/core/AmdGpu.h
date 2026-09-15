// edgeline: GPU telemetry from the amdgpu driver's sysfs interface.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// nvidia-smi is a tool that has to be installed; amdgpu just writes numbers
// into sysfs. Load, VRAM, temperature and power are all plain files under the
// card's device directory, readable by any user, no helper needed.
//
// The root directory is a parameter so a test can point this at a fake tree
// and check the selection logic without owning three graphics cards.
#pragma once

#include <QList>
#include <QString>

namespace xen {

struct AmdGpuSample {
    QString card;          // "card2"
    QString busAddress;    // "0000:72:00.0", empty when not resolvable
    QString deviceId;      // "13c0", from the PCI device id
    double busyPct = -1;
    double vramUsedGiB = -1;
    double vramTotalGiB = -1;
    double tempC = -1;
    double powerW = -1;

    [[nodiscard]] bool valid() const { return !card.isEmpty(); }
};

// Every card whose PCI vendor is AMD. Cards without the amdgpu attributes
// (an old radeon-driver part, or a card that is powered off) are skipped
// rather than reported with nothing in them.
QList<AmdGpuSample> readAmdGpus(const QString& drmRoot = QStringLiteral("/sys/class/drm"));

// The card to show when there are several: the one with the most VRAM, which
// on a desktop means the discrete card rather than the CPU's integrated one.
const AmdGpuSample* pickAmdGpu(const QList<AmdGpuSample>& cards);

} // namespace xen
