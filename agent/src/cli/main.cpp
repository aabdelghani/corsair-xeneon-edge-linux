// edgeline: command line for the Corsair Xeneon Edge on Linux.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "transport/HidEnumerator.h"
#include "transport/HidRecon.h"
#include "core/AppSettings.h"
#include "ipc/RpcClient.h"
#include "core/UpdateChecker.h"
#include "x11/TouchProbe.h"

#include <QCoreApplication>
#include <QJsonObject>
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
        "usage: %s <command> [args]\n"
        "\n"
        "\nTalking to the agent (edgeline-agent must be running):\n"
        "  status              panel, DDC and touch state at a glance\n"
        "  get <property>      read a picture value\n"
        "  set <property> <n>  write a picture value\n"
        "  reset <scope>       restore panel defaults\n"
        "                      factory | brightness | colour\n"
        "  touch mode [<mode>] show or set the touch mode\n"
        "                      off | main-cursor | own-pointer | ripple\n"
        "\nDirect hardware inspection (no agent needed):\n"
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
    xen::settings::init();

    std::printf("Installed: %s\n", EDGELINE_VERSION);
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

// Properties the design's CLI examples name, mapped to the VCP codes the panel
// actually implements. Ranges are not hardcoded: the agent reports the real
// maximum, so `set sharpness 9` is refused on a panel whose maximum is 4
// instead of being clamped silently.
struct Prop {
    const char* name;
    int code;
};
static const Prop kProps[] = {
    { "brightness", 0x10 }, { "contrast", 0x12 }, { "preset", 0x14 },
    { "red", 0x16 },        { "green", 0x18 },    { "blue", 0x1A },
    { "sharpness", 0x87 },  { "input", 0x60 },
};

static const Prop* findProp(const char* name)
{
    for (const Prop& p : kProps)
        if (std::strcmp(p.name, name) == 0)
            return &p;
    return nullptr;
}

static void listProps(std::FILE* out)
{
    std::fprintf(out, "properties:");
    for (const Prop& p : kProps)
        std::fprintf(out, " %s", p.name);
    std::fputc('\n', out);
}

static int cmdStatus(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto rep = xen::RpcClient::call(QStringLiteral("state.all"));
    if (!rep.ok) {
        std::fprintf(stderr, "%s\n", rep.error.toUtf8().constData());
        return 3;
    }
    const QJsonObject dev = rep.result.value(QStringLiteral("device")).toObject();
    const QJsonObject ddc = rep.result.value(QStringLiteral("ddc")).toObject();
    const QJsonObject touch = rep.result.value(QStringLiteral("touch")).toObject();
    const QJsonObject sys = rep.result.value(QStringLiteral("system")).toObject();

    std::printf("edgeline %s\n", sys.value(QStringLiteral("version")).toString().toUtf8().constData());
    std::printf("  panel:   %s%s\n",
                dev.value(QStringLiteral("present")).toBool() ? "connected" : "not found",
                dev.value(QStringLiteral("present")).toBool()
                    && !dev.value(QStringLiteral("accessible")).toBool()
                    ? " (hidraw not accessible)" : "");
    std::printf("  ddc:     %s\n", ddc.value(QStringLiteral("message")).toString().toUtf8().constData());
    std::printf("  touch:   %s\n", touch.value(QStringLiteral("mode")).toString().toUtf8().constData());

    const QJsonObject vals = ddc.value(QStringLiteral("values")).toObject();
    for (const Prop& p : kProps) {
        const QString key = QStringLiteral("%1").arg(p.code, 2, 16, QLatin1Char('0'));
        if (!vals.contains(key))
            continue;
        const QJsonObject v = vals.value(key).toObject();
        const int max = v.value(QStringLiteral("max")).toInt();
        if (max > 0)
            std::printf("  %-10s %d / %d\n", p.name, v.value(QStringLiteral("value")).toInt(), max);
        else
            std::printf("  %-10s 0x%02x\n", p.name, v.value(QStringLiteral("value")).toInt());
    }
    return 0;
}

static int cmdSet(int argc, char** argv)
{
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s set <property> <value>\n", argv[0]);
        listProps(stderr);
        return 64;
    }
    const Prop* prop = findProp(argv[2]);
    if (!prop) {
        std::fprintf(stderr, "%s: unknown property '%s'\n", argv[0], argv[2]);
        listProps(stderr);
        return 64;
    }
    char* end = nullptr;
    const long value = std::strtol(argv[3], &end, 0);
    if (end == argv[3] || *end != '\0' || value < 0) {
        std::fprintf(stderr, "%s: '%s' is not a number\n", argv[0], argv[3]);
        return 64;
    }

    QCoreApplication app(argc, argv);

    // Check the value against what the panel reports before writing, so a bad
    // number is refused here rather than failing slowly inside ddcutil.
    const auto st = xen::RpcClient::call(QStringLiteral("ddc.state"));
    if (st.ok) {
        const QJsonObject v = st.result.value(QStringLiteral("values")).toObject()
                                  .value(QStringLiteral("%1").arg(prop->code, 2, 16, QLatin1Char('0')))
                                  .toObject();
        const int max = v.value(QStringLiteral("max")).toInt();
        if (max > 0 && value > max) {
            std::fprintf(stderr, "%s: %s accepts 0..%d on this panel\n", argv[0], prop->name, max);
            return 65;
        }
    }

    const auto rep = xen::RpcClient::call(
        QStringLiteral("ddc.set"),
        QJsonObject{ { QStringLiteral("code"), prop->code },
                     { QStringLiteral("value"), int(value) } });
    if (!rep.ok) {
        std::fprintf(stderr, "%s\n", rep.error.toUtf8().constData());
        return 3;
    }
    std::printf("%s = %ld\n", prop->name, value);
    return 0;
}

static int cmdGet(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s get <property>\n", argv[0]);
        listProps(stderr);
        return 64;
    }
    const Prop* prop = findProp(argv[2]);
    if (!prop) {
        std::fprintf(stderr, "%s: unknown property '%s'\n", argv[0], argv[2]);
        listProps(stderr);
        return 64;
    }
    QCoreApplication app(argc, argv);
    const auto rep = xen::RpcClient::call(QStringLiteral("ddc.state"));
    if (!rep.ok) {
        std::fprintf(stderr, "%s\n", rep.error.toUtf8().constData());
        return 3;
    }
    const QJsonObject v = rep.result.value(QStringLiteral("values")).toObject()
                              .value(QStringLiteral("%1").arg(prop->code, 2, 16, QLatin1Char('0')))
                              .toObject();
    if (v.isEmpty()) {
        std::fprintf(stderr, "%s: the agent has no value for %s yet\n", argv[0], prop->name);
        return 1;
    }
    std::printf("%d\n", v.value(QStringLiteral("value")).toInt());
    return 0;
}

static int cmdTouchMode(int argc, char** argv)
{
    // `edgeline touch mode own-pointer`
    QCoreApplication app(argc, argv);
    if (argc < 4) {
        const auto rep = xen::RpcClient::call(QStringLiteral("touch.state"));
        if (!rep.ok) {
            std::fprintf(stderr, "%s\n", rep.error.toUtf8().constData());
            return 3;
        }
        std::printf("%s\n", rep.result.value(QStringLiteral("mode")).toString().toUtf8().constData());
        return 0;
    }
    const auto rep = xen::RpcClient::call(
        QStringLiteral("touch.setMode"),
        QJsonObject{ { QStringLiteral("mode"), QString::fromLocal8Bit(argv[3]) } });
    if (!rep.ok) {
        std::fprintf(stderr, "%s\n", rep.error.toUtf8().constData());
        return 3;
    }
    std::printf("touch mode = %s\n",
                rep.result.value(QStringLiteral("mode")).toString().toUtf8().constData());
    return 0;
}

static int cmdReset(int argc, char** argv)
{
    // The panel implements three separate restore commands, and the narrow ones
    // are the useful ones: "colour" put this panel's RGB gain back to its
    // factory values after a profile apply had reset them, without touching
    // brightness or anything else.
    static const struct { const char* name; const char* what; } kScopes[] = {
        { "factory",    "every picture setting" },
        { "brightness", "brightness and contrast only" },
        { "colour",     "colour preset and RGB gain only" },
    };

    if (argc < 3) {
        std::fprintf(stderr, "usage: %s reset <scope>\n\nscopes:\n", argv[0]);
        for (const auto& s : kScopes)
            std::fprintf(stderr, "  %-11s %s\n", s.name, s.what);
        return 64;
    }

    QString scope = QString::fromLocal8Bit(argv[2]).toLower();
    // Accept both spellings rather than being pedantic about it.
    if (scope == QLatin1String("color"))
        scope = QStringLiteral("colour");

    bool known = false;
    for (const auto& s : kScopes)
        if (scope == QLatin1String(s.name))
            known = true;
    if (!known) {
        std::fprintf(stderr, "%s: unknown scope '%s'\n", argv[0], argv[2]);
        for (const auto& s : kScopes)
            std::fprintf(stderr, "  %-11s %s\n", s.name, s.what);
        return 64;
    }

    QCoreApplication app(argc, argv);
    const auto rep = xen::RpcClient::call(
        QStringLiteral("ddc.restoreDefaults"),
        // The agent speaks the American spelling on the wire.
        QJsonObject{ { QStringLiteral("scope"),
                       scope == QLatin1String("colour") ? QStringLiteral("color") : scope } });
    if (!rep.ok) {
        std::fprintf(stderr, "%s\n", rep.error.toUtf8().constData());
        return 3;
    }
    std::printf("restored: %s\n", scope.toUtf8().constData());
    return 0;
}

int main(int argc, char** argv)
{
    if (argc >= 2) {
        if (std::strcmp(argv[1], "--version") == 0 || std::strcmp(argv[1], "-V") == 0) {
            std::printf("edgeline %s\n", EDGELINE_VERSION);
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

    if (std::strcmp(argv[1], "touch") == 0) {
        // `touch mode [...]` talks to the agent; bare `touch` is the read-only
        // hardware probe. Order matters: the probe would otherwise swallow it.
        if (argc >= 3 && std::strcmp(argv[2], "mode") == 0)
            return cmdTouchMode(argc, argv);
        return cmdTouch(argc, argv);
    }

    if (std::strcmp(argv[1], "update-check") == 0)
        return cmdUpdateCheck(argc, argv);

    if (std::strcmp(argv[1], "status") == 0)
        return cmdStatus(argc, argv);

    if (std::strcmp(argv[1], "set") == 0)
        return cmdSet(argc, argv);

    if (std::strcmp(argv[1], "get") == 0)
        return cmdGet(argc, argv);

    if (std::strcmp(argv[1], "reset") == 0)
        return cmdReset(argc, argv);


    printUsage(stderr, argv[0]);
    return 64;
}
