// SPDX-License-Identifier: GPL-3.0-or-later
// Reads a fake sysfs tree, because the alternative is owning three graphics
// cards. The shape of the tree is copied from a real amdgpu device directory.
#include "core/AmdGpu.h"

#include "check.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>

using namespace xen;

namespace {

void put(const QString& path, const QString& text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    f.write((text + QLatin1Char('\n')).toUtf8());
}

bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString root = tmp.path();

    // card0: an integrated part with a 2 GiB carve-out, like the one this was
    // developed against.
    put(root + "/card0/device/vendor", "0x1002");
    put(root + "/card0/device/device", "0x13c0");
    put(root + "/card0/device/gpu_busy_percent", "7");
    put(root + "/card0/device/mem_info_vram_used", "55566336");
    put(root + "/card0/device/mem_info_vram_total", "2147483648");
    put(root + "/card0/device/hwmon/hwmon7/name", "amdgpu");
    put(root + "/card0/device/hwmon/hwmon7/temp1_input", "40000");
    put(root + "/card0/device/hwmon/hwmon7/power1_input", "11000000");

    // card1: a discrete card with 16 GiB and the newer power attribute name.
    put(root + "/card1/device/vendor", "0x1002");
    put(root + "/card1/device/device", "0x7590");
    put(root + "/card1/device/gpu_busy_percent", "63");
    put(root + "/card1/device/mem_info_vram_used", "4294967296");
    put(root + "/card1/device/mem_info_vram_total", "17179869184");
    put(root + "/card1/device/hwmon/hwmon3/name", "amdgpu");
    put(root + "/card1/device/hwmon/hwmon3/temp1_input", "61000");
    put(root + "/card1/device/hwmon/hwmon3/power1_average", "148000000");

    // card2: NVIDIA. Must be ignored no matter what files it has.
    put(root + "/card2/device/vendor", "0x10de");
    put(root + "/card2/device/gpu_busy_percent", "99");
    put(root + "/card2/device/mem_info_vram_total", "34359738368");

    // card3: AMD vendor but nothing the amdgpu driver writes, e.g. an old part
    // on the radeon driver. Must be skipped rather than reported empty.
    put(root + "/card3/device/vendor", "0x1002");
    put(root + "/card3/device/device", "0x6779");

    // Connector entries live beside the cards and must not be mistaken for one.
    put(root + "/card1-DP-1/status", "connected");

    const QList<AmdGpuSample> cards = readAmdGpus(root);
    CHECK(cards.size() == 2);

    const AmdGpuSample* igpu = nullptr;
    const AmdGpuSample* dgpu = nullptr;
    for (const AmdGpuSample& c : cards) {
        if (c.card == "card0") igpu = &c;
        if (c.card == "card1") dgpu = &c;
    }
    CHECK(igpu && dgpu);

    CHECK(igpu->deviceId == "13c0");
    CHECK(near(igpu->busyPct, 7));
    CHECK(near(igpu->vramTotalGiB, 2.0));
    CHECK(near(igpu->tempC, 40.0));
    CHECK(near(igpu->powerW, 11.0));      // power1_input, microwatts
    CHECK(igpu->busAddress.isEmpty());    // a fixture dir has no PCI address, and that is fine

    CHECK(dgpu->deviceId == "7590");
    CHECK(near(dgpu->busyPct, 63));
    CHECK(near(dgpu->vramUsedGiB, 4.0));
    CHECK(near(dgpu->vramTotalGiB, 16.0));
    CHECK(near(dgpu->tempC, 61.0));
    CHECK(near(dgpu->powerW, 148.0));     // power1_average preferred when present

    // With both present, the discrete card is the one to show.
    const AmdGpuSample* pick = pickAmdGpu(cards);
    CHECK(pick && pick->card == "card1");

    // Missing sensors degrade to -1, not to zero and not to a skipped card.
    put(root + "/card4/device/vendor", "0x1002");
    put(root + "/card4/device/device", "0x744c");
    put(root + "/card4/device/gpu_busy_percent", "12");
    const QList<AmdGpuSample> more = readAmdGpus(root);
    CHECK(more.size() == 3);
    for (const AmdGpuSample& c : more)
        if (c.card == "card4") {
            CHECK(near(c.busyPct, 12));
            CHECK(c.vramTotalGiB < 0);
            CHECK(c.tempC < 0);
            CHECK(c.powerW < 0);
        }

    CHECK(readAmdGpus(root + "/does-not-exist").isEmpty());
    CHECK(pickAmdGpu({}) == nullptr);

    return xen::test::report("test_amdgpu");
}
