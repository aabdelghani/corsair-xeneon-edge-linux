// edgeline: command line for the Corsair Xeneon Edge on Linux.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "transport/HidEnumerator.h"
#include "transport/HidRecon.h"
#include "core/AppSettings.h"
#include "core/SensorSource.h"
#include "ipc/RpcClient.h"
#include "ipc/RpcServer.h"
#include "core/UpdateChecker.h"
#include "x11/TouchProbe.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QSocketNotifier>
#include <QStringList>
#include <QTimer>

#include <csignal>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <unistd.h>

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
        "  gain <r> <g> <b>    set the RGB gain in one go\n"
        "  profile <sub>       list | show <name> | save <name> [--overwrite]\n"
        "                      | apply <name> | delete <name> | rename <from> <to>\n"
        "  status --json       the full state as JSON (get <property> --json likewise)\n"
        "\nFor scripts:\n"
        "  call [--compact] <method> [<json-object> | -]\n"
        "                      call any agent method, params from the argument or stdin\n"
        "  methods             list every method the agent answers\n"
        "  watch [<event>...]  print pushed events as JSON lines until interrupted\n"
        "\nDirect hardware inspection (no agent needed):\n"
        "  list    identify the Edge and check hidraw access\n"
        "  probe   read-only HID reconnaissance (sends nothing)\n"
        "  touch   report the touch stack; --live measures real contacts\n"
        "  gpus    every GPU found, and which one the dashboard shows\n"
        "  update-check  ask GitHub whether a newer release exists\n"
        "\n"
        "  --version   print the version and exit\n"
        "  --help      print this message and exit\n",
        argv0);
}

// Both vendors, read the same way the dashboard reads them. No agent and no
// panel: this is nvidia-smi, amdgpu sysfs and lspci, so it answers on a machine
// with no Edge attached and tells you what the tile would show and why.
static int cmdGpus(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    xen::settings::init();

    xen::SensorSource sensors;
    const QList<xen::GpuInfo> all = sensors.enumerateGpus();
    if (all.isEmpty()) {
        std::puts("No GPU telemetry. nvidia-smi is not answering and no amdgpu card "
                  "is reporting through sysfs.");
        return 1;
    }

    const QStringList saved = xen::settings::loadGpuSelection();
    QStringList shownIds;
    for (const xen::GpuInfo& g : xen::selectGpus(all, saved))
        shownIds << g.id;

    std::printf("GPUS  (dashboard tile: %s)\n\n",
                saved.isEmpty() ? "automatic, the card with the most VRAM" : "chosen");
    for (const xen::GpuInfo& g : all) {
        std::printf("%s %s\n", shownIds.contains(g.id) ? "*" : " ",
                    g.name.toUtf8().constData());
        std::printf("    id:     %s\n", g.id.toUtf8().constData());
        std::printf("    source: %s\n", g.source.toUtf8().constData());
        if (g.utilPct >= 0)
            std::printf("    load:   %.0f%%\n", g.utilPct);
        if (g.memTotalGiB >= 0)
            std::printf("    vram:   %.1f of %.1f GiB\n",
                        g.memUsedGiB >= 0 ? g.memUsedGiB : 0.0, g.memTotalGiB);
        if (g.tempC >= 0)
            std::printf("    temp:   %.0f C\n", g.tempC);
        // Printed to three decimals because an integrated part really does
        // report figures like 0.009 W, and rounding that to "0 W" reads as a
        // driver that is not measuring rather than one that is.
        if (g.powerW >= 0)
            std::printf("    power:  %.3f W\n", g.powerW);
        std::putchar('\n');
    }
    std::printf("* shown on the panel dashboard. Change it on the Dashboard page.\n");
    return 0;
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

// Shared by the commands below: the error line and the exit code that goes
// with it, and a JSON object printed the way `call` prints it.
static int failRpc(const xen::RpcClient::Reply& rep)
{
    std::fprintf(stderr, "%s\n", rep.error.toUtf8().constData());
    return 3;
}

static void printJson(const QJsonObject& obj, bool compact = false)
{
    const QByteArray text = QJsonDocument(obj).toJson(compact ? QJsonDocument::Compact
                                                              : QJsonDocument::Indented);
    std::fwrite(text.constData(), 1, size_t(text.size()), stdout);
    if (compact)
        std::fputc('\n', stdout);
    std::fflush(stdout);
}

// True when `flag` is among the arguments after the command word. The flags
// of this CLI may stand anywhere, so there is no positional parsing to get wrong.
static bool hasFlag(int argc, char** argv, const char* flag)
{
    for (int i = 2; i < argc; ++i)
        if (std::strcmp(argv[i], flag) == 0)
            return true;
    return false;
}

static int cmdStatus(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto rep = xen::RpcClient::call(QStringLiteral("state.all"));
    if (!rep.ok)
        return failRpc(rep);
    if (hasFlag(argc, argv, "--json")) {
        printJson(rep.result);
        return 0;
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
    // `--json` may stand before or after the property.
    const bool json = hasFlag(argc, argv, "--json");
    const char* name = nullptr;
    for (int i = 2; i < argc && !name; ++i)
        if (std::strcmp(argv[i], "--json") != 0)
            name = argv[i];
    if (!name) {
        std::fprintf(stderr, "usage: %s get <property> [--json]\n", argv[0]);
        listProps(stderr);
        return 64;
    }
    const Prop* prop = findProp(name);
    if (!prop) {
        std::fprintf(stderr, "%s: unknown property '%s'\n", argv[0], name);
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
    if (json) {
        printJson(v);
        return 0;
    }
    std::printf("%d\n", v.value(QStringLiteral("value")).toInt());
    return 0;
}

// `edgeline gain <r> <g> <b>`: the three channels are separate VCP codes, so
// setting a colour temperature by hand is three `set` calls; this does them
// together and checks all three against the panel before writing any, so a
// bad blue value cannot leave red and green already changed.
static int cmdGain(int argc, char** argv)
{
    if (argc != 5) {
        std::fprintf(stderr, "usage: %s gain <red> <green> <blue>\n", argv[0]);
        return 64;
    }
    static const Prop* const kChannels[] = { findProp("red"), findProp("green"), findProp("blue") };
    long values[3] = {};
    for (int i = 0; i < 3; ++i) {
        char* end = nullptr;
        errno = 0;
        values[i] = std::strtol(argv[2 + i], &end, 0);
        if (end == argv[2 + i] || *end != '\0' || values[i] < 0 || values[i] > INT_MAX || errno != 0) {
            std::fprintf(stderr, "%s: '%s' is not a number\n", argv[0], argv[2 + i]);
            return 64;
        }
    }

    QCoreApplication app(argc, argv);

    // As in `set`: when the range cannot be read, write anyway and let the
    // agent be the judge.
    const auto st = xen::RpcClient::call(QStringLiteral("ddc.state"));
    if (st.ok) {
        const QJsonObject all = st.result.value(QStringLiteral("values")).toObject();
        for (int i = 0; i < 3; ++i) {
            const int max = all.value(QStringLiteral("%1").arg(kChannels[i]->code, 2, 16, QLatin1Char('0')))
                                .toObject().value(QStringLiteral("max")).toInt();
            if (max > 0 && values[i] > max) {
                std::fprintf(stderr, "%s: %s accepts 0..%d on this panel\n", argv[0],
                             kChannels[i]->name, max);
                return 65;
            }
        }
    }

    for (int i = 0; i < 3; ++i) {
        const auto rep = xen::RpcClient::call(
            QStringLiteral("ddc.set"),
            QJsonObject{ { QStringLiteral("code"), kChannels[i]->code },
                         { QStringLiteral("value"), int(values[i]) } });
        if (!rep.ok)
            return failRpc(rep);
    }
    std::printf("gain = %ld %ld %ld\n", values[0], values[1], values[2]);
    return 0;
}

// `edgeline call [--compact] <method> [<json-object> | -]`: the escape hatch to
// every agent method, so a script needs neither socat nor the wire format.
static int cmdCall(int argc, char** argv)
{
    bool compact = false;
    QStringList pos;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--compact") == 0)
            compact = true;
        else
            pos << QString::fromLocal8Bit(argv[i]);
    }
    if (pos.isEmpty() || pos.size() > 2) {
        std::fprintf(stderr, "usage: %s call [--compact] <method> [<json-object> | -]\n", argv[0]);
        return 64;
    }

    QJsonObject params;
    if (pos.size() == 2) {
        QByteArray text;
        if (pos[1] == QLatin1String("-")) {
            char buf[4096];
            size_t n = 0;
            while ((n = std::fread(buf, 1, sizeof buf, stdin)) > 0)
                text.append(buf, qsizetype(n));
        } else {
            text = pos[1].toUtf8();
        }
        QJsonParseError perr{};
        const QJsonDocument doc = QJsonDocument::fromJson(text, &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            std::fprintf(stderr, "%s: the parameters must be a JSON object%s%s\n", argv[0],
                         perr.error != QJsonParseError::NoError ? ": " : "",
                         perr.error != QJsonParseError::NoError
                             ? perr.errorString().toUtf8().constData() : "");
            return 64;
        }
        params = doc.object();
    }

    QCoreApplication app(argc, argv);
    const auto rep = xen::RpcClient::call(pos[0], params);
    if (!rep.ok)
        return failRpc(rep);
    printJson(rep.result, compact);
    return 0;
}

static int cmdMethods(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto rep = xen::RpcClient::call(QStringLiteral("rpc.methods"));
    if (!rep.ok)
        return failRpc(rep);
    for (const QJsonValue& v : rep.result.value(QStringLiteral("methods")).toArray())
        std::printf("%s\n", v.toString().toUtf8().constData());
    return 0;
}

// Signals reach the event loop through a pipe: a handler may only do
// async-signal-safe things, and writing one byte is the classic one. The read
// end sits in a QSocketNotifier, so the loop wakes and ends the watch cleanly
// with exit 0, which is what Ctrl-C on a `watch | jq` pipeline should mean.
static int g_signalPipe[2] = { -1, -1 };

static void onSignal(int)
{
    const char b = 1;
    const ssize_t ignored = ::write(g_signalPipe[1], &b, 1);
    (void)ignored;
}

// `edgeline watch [<event>...]`. Deliberately sends no request: sensors.stream
// and touch.stream are switches shared by every client of the agent, and a
// passive listener must not turn them on or, worse, off for the window.
static int cmdWatch(int argc, char** argv)
{
    QStringList wanted;
    for (int i = 2; i < argc; ++i)
        wanted << QString::fromLocal8Bit(argv[i]);

    QCoreApplication app(argc, argv);

    QLocalSocket sock;
    const QString path = xen::RpcServer::defaultSocketPath();
    sock.connectToServer(path);
    if (!sock.waitForConnected(2000)) {
        std::fprintf(stderr, "no agent listening on %s (start edgeline-agent)\n",
                     path.toUtf8().constData());
        return 3;
    }

    if (::pipe(g_signalPipe) != 0) {
        std::fprintf(stderr, "%s: cannot create a pipe: %s\n", argv[0], std::strerror(errno));
        return 3;
    }
    struct sigaction sa {};
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    QSocketNotifier notifier(g_signalPipe[0], QSocketNotifier::Read);
    QObject::connect(&notifier, &QSocketNotifier::activated, &app,
                     []() { QCoreApplication::exit(0); });

    QByteArray buffer;
    QObject::connect(&sock, &QLocalSocket::readyRead, &app, [&]() {
        buffer.append(sock.readAll());
        qsizetype nl = 0;
        while ((nl = buffer.indexOf('\n')) >= 0) {
            const QByteArray line = buffer.left(nl);
            buffer.remove(0, nl + 1);
            const QJsonObject obj = QJsonDocument::fromJson(line).object();
            const QString event = obj.value(QStringLiteral("event")).toString();
            if (event.isEmpty() || (!wanted.isEmpty() && !wanted.contains(event)))
                continue;
            const QByteArray out = QJsonDocument(obj).toJson(QJsonDocument::Compact);
            std::fwrite(out.constData(), 1, size_t(out.size()), stdout);
            std::fputc('\n', stdout);
            // Flush every line: stdout on a pipe is block-buffered, and a
            // consumer waiting for the next event would otherwise wait for 4 KiB.
            std::fflush(stdout);
        }
    });
    QObject::connect(&sock, &QLocalSocket::disconnected, &app, []() {
        std::fprintf(stderr, "the agent closed the connection\n");
        QCoreApplication::exit(3);
    });

    const int rc = QCoreApplication::exec();
    // Leaving scope destroys the socket, which closes it and emits
    // `disconnected`; the lambda above lives on the app context and would
    // still print "the agent closed the connection" after a clean signal exit.
    sock.disconnect();
    return rc;
}

// `edgeline profile list|show|save|apply|delete|rename`: thin wrappers over
// profiles.*, so the common profile chores need no JSON typed by hand.
static int cmdProfile(int argc, char** argv)
{
    static const char* const kUsage =
        "usage: %s profile list | show <name> | save <name> [--overwrite] | apply <name>\n"
        "                  | delete <name> | rename <from> <to>\n";
    if (argc < 3) {
        std::fprintf(stderr, kUsage, argv[0]);
        return 64;
    }
    const QString sub = QString::fromLocal8Bit(argv[2]);

    bool overwrite = false;
    QStringList args;
    for (int i = 3; i < argc; ++i) {
        if (sub == QLatin1String("save") && std::strcmp(argv[i], "--overwrite") == 0)
            overwrite = true;
        else
            args << QString::fromLocal8Bit(argv[i]);
    }

    QString method;
    QJsonObject params;
    int wantArgs = 1;
    if (sub == QLatin1String("list")) {
        method = QStringLiteral("profiles.list");
        wantArgs = 0;
    } else if (sub == QLatin1String("show")) {
        method = QStringLiteral("profiles.get");
    } else if (sub == QLatin1String("save")) {
        method = QStringLiteral("profiles.save");
    } else if (sub == QLatin1String("apply")) {
        method = QStringLiteral("profiles.apply");
    } else if (sub == QLatin1String("delete")) {
        method = QStringLiteral("profiles.delete");
    } else if (sub == QLatin1String("rename")) {
        method = QStringLiteral("profiles.rename");
        wantArgs = 2;
    } else {
        std::fprintf(stderr, "%s: unknown profile command '%s'\n", argv[0], argv[2]);
        std::fprintf(stderr, kUsage, argv[0]);
        return 64;
    }
    if (args.size() != wantArgs) {
        std::fprintf(stderr, kUsage, argv[0]);
        return 64;
    }
    if (wantArgs == 1)
        params.insert(QStringLiteral("name"), args[0]);
    if (wantArgs == 2) {
        params.insert(QStringLiteral("from"), args[0]);
        params.insert(QStringLiteral("to"), args[1]);
    }
    if (overwrite)
        params.insert(QStringLiteral("overwrite"), true);

    QCoreApplication app(argc, argv);
    const auto rep = xen::RpcClient::call(method, params);
    if (!rep.ok)
        return failRpc(rep);

    if (sub == QLatin1String("list")) {
        const QString active = rep.result.value(QStringLiteral("active")).toString();
        for (const QJsonValue& v : rep.result.value(QStringLiteral("profiles")).toArray()) {
            const QString name = v.toObject().value(QStringLiteral("name")).toString();
            std::printf("%s%s\n", !active.isEmpty() && name == active ? "* " : "  ",
                        name.toUtf8().constData());
        }
    } else if (sub == QLatin1String("show")) {
        printJson(rep.result);
    } else if (sub == QLatin1String("rename")) {
        std::printf("renamed: %s -> %s\n", args[0].toUtf8().constData(), args[1].toUtf8().constData());
    } else {
        // saved / applied / deleted
        std::printf("%s: %s\n", sub == QLatin1String("save") ? "saved"
                                : sub == QLatin1String("apply") ? "applied" : "deleted",
                    args[0].toUtf8().constData());
    }
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

    if (std::strcmp(argv[1], "gpus") == 0)
        return cmdGpus(argc, argv);

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

    if (std::strcmp(argv[1], "call") == 0)
        return cmdCall(argc, argv);

    if (std::strcmp(argv[1], "methods") == 0)
        return cmdMethods(argc, argv);

    if (std::strcmp(argv[1], "watch") == 0)
        return cmdWatch(argc, argv);

    if (std::strcmp(argv[1], "profile") == 0)
        return cmdProfile(argc, argv);

    if (std::strcmp(argv[1], "gain") == 0)
        return cmdGain(argc, argv);


    printUsage(stderr, argv[0]);
    return 64;
}
