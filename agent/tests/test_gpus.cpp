// SPDX-License-Identifier: GPL-3.0-or-later
// Two-vendor machines are the point of this code, and the machine it was
// written on has exactly one of each. Both parsing and selection are pure, so
// the cases that matter (two NVIDIA cards, a card that reports no power, a
// saved choice pointing at a card that has since been removed) are tested
// against fixed text instead of against whatever hardware is to hand.
#include "core/Gpus.h"

#include "check.h"

#include <QCoreApplication>

#include <cmath>
#include <cstdio>

using namespace xen;

namespace {
bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // ---------------------------------------------------------- nvidia-smi
    // Verbatim from this machine, plus a second card to make it a dual.
    const QString twoCards =
        "GPU-8d7cf61a-e7d0-8304-b0b0-7d2bf89ae4f5, NVIDIA GeForce RTX 5090, 47, 0, 1369, 32607, 30.46\n"
        "GPU-11111111-2222-3333-4444-555555555555, NVIDIA GeForce RTX 3060, 51, 88, 4096, 12288, 140.00\n";
    const QList<GpuInfo> nv = parseNvidiaSmi(twoCards);
    CHECK(nv.size() == 2);
    CHECK(nv[0].id == "nvidia:GPU-8d7cf61a-e7d0-8304-b0b0-7d2bf89ae4f5");
    CHECK(nv[0].name == "NVIDIA GeForce RTX 5090");
    CHECK(nv[0].source == "nvidia-smi");
    CHECK(near(nv[0].tempC, 47));
    CHECK(near(nv[0].utilPct, 0));
    CHECK(near(nv[0].memTotalGiB, 32607.0 / 1024.0));
    CHECK(near(nv[0].powerW, 30.46));
    CHECK(nv[1].name == "NVIDIA GeForce RTX 3060");
    CHECK(near(nv[1].utilPct, 88));

    // The id is the UUID, never the index, so two identical cards stay
    // distinguishable and neither moves when one is removed.
    CHECK(nv[0].id != nv[1].id);

    // "[N/A]" is what nvidia-smi prints for a figure the card does not report.
    // It is not zero, and a tile showing "0 W" for it would be a lie.
    const QList<GpuInfo> na = parseNvidiaSmi(
        "GPU-abc, NVIDIA T400, [N/A], 3, 100, 2048, [N/A]\n");
    CHECK(na.size() == 1);
    CHECK(na[0].tempC < 0);
    CHECK(na[0].powerW < 0);
    CHECK(near(na[0].utilPct, 3));

    // A model name containing a comma must not eat the numbers. The five
    // trailing columns are fixed, so the name is whatever is between.
    const QList<GpuInfo> comma = parseNvidiaSmi(
        "GPU-xyz, Quadro RTX 8000, Passive, 60, 10, 512, 49152, 70.5\n");
    CHECK(comma.size() == 1);
    CHECK(comma[0].name == "Quadro RTX 8000, Passive");
    CHECK(near(comma[0].powerW, 70.5));
    CHECK(near(comma[0].tempC, 60));

    // Junk is skipped rather than turned into a card with nothing in it.
    CHECK(parseNvidiaSmi("").isEmpty());
    CHECK(parseNvidiaSmi("bash: nvidia-smi: command not found\n").isEmpty());

    // ------------------------------------------------------------- amdgpu
    AmdGpuSample amd;
    amd.card = "card2";
    amd.busAddress = "0000:72:00.0";
    amd.deviceId = "13c0";
    amd.busyPct = 4;
    amd.vramUsedGiB = 0.4;
    amd.vramTotalGiB = 2;
    amd.tempC = 41;
    amd.powerW = 0.009;
    const GpuInfo ig = fromAmdSample(amd, QStringLiteral("AMD Radeon Graphics"));
    // The bus address, not the card number: adding a card renumbers card2.
    CHECK(ig.id == "amdgpu:0000:72:00.0");
    CHECK(ig.source == "amdgpu");
    CHECK(near(ig.utilPct, 4));
    CHECK(near(ig.memTotalGiB, 2));

    AmdGpuSample noAddr;
    noAddr.card = "card0";
    noAddr.busyPct = 1;
    CHECK(fromAmdSample(noAddr, QStringLiteral("x")).id == "amdgpu:card0");

    // ---------------------------------------------------------- selection
    const QList<GpuInfo> both{ nv[0], ig };   // 32 GiB NVIDIA + 2 GiB Radeon

    // Automatic takes the card with the most VRAM, which is the discrete one.
    const QList<GpuInfo> autoPick = selectGpus(both, {});
    CHECK(autoPick.size() == 1);
    CHECK(autoPick[0].id == nv[0].id);

    // Asking for the integrated one gets exactly it, not the "better" card.
    const QList<GpuInfo> justAmd = selectGpus(both, { ig.id });
    CHECK(justAmd.size() == 1);
    CHECK(justAmd[0].id == ig.id);

    // Both, which is the case this whole change exists for.
    const QList<GpuInfo> pair = selectGpus(both, { ig.id, nv[0].id });
    CHECK(pair.size() == 2);
    // Detection order wins over the order they were asked for, so the tiles do
    // not swap places depending on which checkbox was ticked first.
    CHECK(pair[0].id == nv[0].id);
    CHECK(pair[1].id == ig.id);

    // A repeated id is one tile, not two.
    CHECK(selectGpus(both, { ig.id, ig.id }).size() == 1);

    // A card named in the config but no longer in the machine is dropped.
    const QList<GpuInfo> stale = selectGpus(both, { nv[1].id, ig.id });
    CHECK(stale.size() == 1);
    CHECK(stale[0].id == ig.id);

    // If every saved id is stale, fall back to automatic rather than to an
    // empty tile that looks like broken telemetry.
    const QList<GpuInfo> allStale = selectGpus(both, { QStringLiteral("nvidia:gone") });
    CHECK(allStale.size() == 1);
    CHECK(allStale[0].id == nv[0].id);

    // No GPUs at all is empty, not a crash and not a phantom card.
    CHECK(selectGpus({}, {}).isEmpty());
    CHECK(selectGpus({}, { ig.id }).isEmpty());

    return xen::test::report("test_gpus");
}
