// xeneonctl, CLI for the Corsair Xeneon Edge on Linux.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "transport/HidEnumerator.h"
#include "transport/HidRecon.h"
#include "core/UpdateChecker.h"
#include "x11/TouchProbe.h"

#include <QCoreApplication>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static int cmdList()
{
    auto info = xen::HidEnumerator::findEdge();
    if (!info) {
        std::puts("No Xeneon Edge (1b1c:1d0d) found.");
        return 1;
    }
    std::printf("XENEON EDGE\n");
    std::printf("  product:    %s\n", info->product.c_str());
    std::printf("  serial:     %s\n", info->serial.c_str());
    std::printf("  hidraw:     %s\n", info->path.c_str());
    std::printf("  usage page: 0x%04X\n", info->usagePage);
    std::printf("  access:     %s\n",
                info->accessible ? "OK" : "DENIED (install udev rule, see README)");
    return info->accessible ? 0 : 2;
}

static int cmdTouch(int argc, char** argv)
{
    xen::TouchReport r = xen::TouchProbe::run();

    std::printf("XENEON EDGE TOUCH STACK  (read-only, nothing is written)\n\n");
    if (!r.haveDigitizer) {
        std::printf("%s\n", r.verdict.c_str());
        return 1;
    }

    std::printf("Digitizer USB id: %s  (separate device from the Bragi channel 1b1c:1d0d)\n\n",
                r.usbId.c_str());

    std::printf("Kernel view (HID interfaces):\n");
    for (auto const& i : r.interfaces) {
        std::printf("  %s\n", i.hidId.c_str());
        std::printf("    driver:        %s\n", i.driver.c_str());
        std::printf("    descriptor:    %zu bytes\n", i.descriptorBytes);
        if (i.fingerCollections > 0)
            std::printf("    finger slots:  %d Digitizer/Finger collections declared\n",
                        i.fingerCollections);
        if (i.contactCountMax > 0)
            std::printf("    contact max:   %d (Contact Count Maximum)\n", i.contactCountMax);
        if (i.configReportId >= 0)
            std::printf("    device config: feature report 0x%02X (input-mode switch)\n",
                        i.configReportId);
        if (!i.inputName.empty())
            std::printf("    input node:    %s  \"%s\"\n",
                        i.eventNode.empty() ? "(none)" : i.eventNode.c_str(),
                        i.inputName.c_str());
        else
            std::printf("    input node:    (none, interface produces no evdev device)\n");
    }

    std::printf("\nX server view (touch-capable devices):\n");
    if (r.xdevices.empty()) {
        std::printf("  (none)\n");
    }
    for (auto const& x : r.xdevices) {
        std::printf("  id=%d  \"%s\"%s%s\n", x.id, x.name.c_str(),
                    x.eventNode.empty() ? "" : "  node=",
                    x.eventNode.c_str());
        if (x.hasTouchClass)
            std::printf("    XITouchClass: max %d simultaneous contacts, %s touch\n",
                        x.maxContacts, x.directMode ? "direct" : "dependent");
        else
            std::printf("    XITouchClass: absent (pointer-emulation interface)\n");
    }

    std::printf("\n%s\n", r.verdict.c_str());

    bool live = false;
    int seconds = 10;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--live") == 0) {
            live = true;
            if (i + 1 < argc) {
                int const v = std::atoi(argv[i + 1]);
                if (v > 0) seconds = v;
            }
        }
    }
    if (!live) {
        std::printf("\n%s\n", r.note.c_str());
        return 0;
    }

    std::printf("\nListening %d seconds. Put as many fingers on the Edge as you can.\n", seconds);
    std::fflush(stdout);
    long begins = 0;
    std::string err;
    int const peak = xen::TouchProbe::capturePeak(seconds, &begins, &err);
    if (peak < 0) {
        std::printf("live capture failed: %s\n", err.c_str());
        return 3;
    }
    std::printf("\nMeasured: peak %d simultaneous contacts, %ld touch-begin events.\n",
                peak, begins);
    if (peak <= 1 && begins > 0)
        std::printf("Only one contact at a time arrived. Touches may be routed through the "
                    "pointer-emulation interface rather than the digitizer.\n");
    if (begins == 0)
        std::printf("No touch events arrived at all. Check that the panel was actually "
                    "touched and that the X server sees it.\n");
    return 0;
}

static void printUsage(std::FILE* out, const char* argv0)
{
    std::fprintf(out,
        "usage: %s [list|probe|touch [--live [seconds]]|update-check]\n"
        "\n"
        "  list    identify the Edge and check hidraw access\n"
        "  probe   read-only HID reconnaissance (sends nothing)\n"
        "  touch   report the touch stack; --live measures real contacts\n"
        "  update-check  ask GitHub whether a newer release exists\n"
        "\n"
        "  --version   print the version and exit\n"
        "  --help      print this message and exit\n",
        argv0);
}

static int cmdUpdateCheck(int argc, char** argv)
{
    // Always a manual check: it exists because someone typed it, so it ignores
    // both the daily throttle and the automatic-checks setting, and it reports
    // failures instead of swallowing them.
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("xeneon-ctl"));
    QCoreApplication::setOrganizationName(QStringLiteral("xeneon-ctl"));

    std::printf("Installed: %s\n", XENEON_VERSION);
    std::fflush(stdout);

    xen::UpdateChecker checker;
    int rc = 1;

    QObject::connect(&checker, &xen::UpdateChecker::updateAvailable, &app,
                     [&rc](const QString& version, const QString& url) {
                         std::printf("Available: %s\n%s\n",
                                     version.toUtf8().constData(),
                                     url.toUtf8().constData());
                         rc = 10; // distinct code so scripts can act on it
                         QCoreApplication::quit();
                     });
    QObject::connect(&checker, &xen::UpdateChecker::upToDate, &app,
                     [&rc](const QString& version) {
                         std::printf("Up to date (%s is the latest release).\n",
                                     version.toUtf8().constData());
                         rc = 0;
                         QCoreApplication::quit();
                     });
    QObject::connect(&checker, &xen::UpdateChecker::checkFailed, &app,
                     [&rc](const QString& err) {
                         std::fprintf(stderr, "Check failed: %s\n",
                                      err.toUtf8().constData());
                         rc = 3;
                         QCoreApplication::quit();
                     });

    // Hard backstop so this can never hang a script.
    QTimer::singleShot(15000, &app, []() {
        std::fprintf(stderr, "Check failed: timed out\n");
        QCoreApplication::exit(3);
    });

    checker.check(true);
    const int loopRc = QCoreApplication::exec();
    return loopRc != 0 ? loopRc : rc;
}

int main(int argc, char** argv)
{
    if (argc >= 2) {
        if (std::strcmp(argv[1], "--version") == 0 || std::strcmp(argv[1], "-V") == 0) {
            std::printf("xeneonctl %s\n", XENEON_VERSION);
            return 0;
        }
        if (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0) {
            printUsage(stdout, argv[0]);
            return 0;
        }
    }

    if (argc < 2 || std::strcmp(argv[1], "list") == 0)
        return cmdList();

    if (std::strcmp(argv[1], "probe") == 0) {
        // Read-only reconnaissance. This command NEVER writes to the device.
        xen::ReconReport r = xen::HidRecon::run();
        if (!r.haveInfo) {
            std::puts("No Xeneon Edge found.");
            return 1;
        }
        std::printf("XENEON EDGE  (read-only probe, no writes sent)\n");
        std::printf("  product:    %s\n", r.info.product.c_str());
        std::printf("  serial:     %s\n", r.info.serial.c_str());
        std::printf("  hidraw:     %s\n", r.info.path.c_str());
        std::printf("  usage page: 0x%04X\n", r.info.usagePage);
        std::printf("\nReport descriptor (%zu bytes, %s):\n",
                    r.reportDescriptor.size(), r.descriptorSource.c_str());
        for (size_t i = 0; i < r.reportDescriptor.size(); ++i)
            std::printf("%02X%s", r.reportDescriptor[i],
                        (i + 1) % 16 == 0 ? "\n" : " ");
        if (!r.reportDescriptor.empty() && r.reportDescriptor.size() % 16 != 0)
            std::putchar('\n');
        std::printf("\nDecode:\n%s", r.descriptorDecode.c_str());
        std::printf("\nPassive read: %s\n", r.passiveNote.c_str());
        if (!r.passiveReport.empty()) {
            std::printf("  bytes:");
            for (uint8_t const b : r.passiveReport)
                std::printf(" %02X", b);
            std::putchar('\n');
        }
        return 0;
    }

    if (std::strcmp(argv[1], "touch") == 0)
        return cmdTouch(argc, argv);

    if (std::strcmp(argv[1], "update-check") == 0)
        return cmdUpdateCheck(argc, argv);

    printUsage(stderr, argv[0]);
    return 64;
}
