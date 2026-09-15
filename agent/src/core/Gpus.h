// edgeline: every GPU in the machine, from both vendors at once.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The first version of this asked nvidia-smi, and only looked at amdgpu sysfs
// once nvidia-smi had failed. That is right for a single-vendor machine and
// wrong for the common desktop case: an NVIDIA card in the slot and a Radeon
// inside the CPU. There, nvidia-smi succeeds, so the Radeon was never looked
// for and the dashboard could not have shown it even if asked.
//
// So both sources are read every poll and merged into one list, and the
// dashboard is told which of them to show.
//
// Parsing and selection are pure functions over plain data, so a two-card
// machine can be tested on a machine that has one.
#pragma once

#include "core/AmdGpu.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace xen {

struct GpuInfo {
    // Stable across reboots and across cards being added: an NVIDIA UUID or a
    // PCI bus address, never an index. "card0" and "GPU 0" both renumber when
    // hardware changes, which would silently repoint the user's choice at a
    // different card.
    QString id;
    QString name;
    QString source;        // "nvidia-smi" or "amdgpu"
    double utilPct = -1;
    double tempC = -1;
    double memUsedGiB = -1;
    double memTotalGiB = -1;
    double powerW = -1;

    [[nodiscard]] bool valid() const { return !id.isEmpty(); }
};

// Parses the output of nvidia-smi --format=csv,noheader,nounits with
// --query-gpu=uuid,name,temperature.gpu,utilization.gpu,memory.used,
// memory.total,power.draw. One line per GPU. "[N/A]", which is what nvidia-smi prints for a figure a
// card does not report, becomes -1 rather than 0.
QList<GpuInfo> parseNvidiaSmi(const QString& out);

// An amdgpu sysfs sample as a GpuInfo. The name is passed in because resolving
// it needs lspci, which is not this function's business.
GpuInfo fromAmdSample(const AmdGpuSample& s, const QString& name);

// The GPUs the dashboard should show, in detection order.
//
// An empty selection means automatic: one card, the one with the most VRAM,
// which on a desktop is the discrete card rather than the CPU's integrated
// one. Ids that are no longer present are dropped, because a card that was
// unplugged should not leave the tile showing nothing. If that drops all of
// them, this falls back to automatic rather than to an empty dashboard.
QList<GpuInfo> selectGpus(const QList<GpuInfo>& all, const QStringList& wanted);

} // namespace xen
