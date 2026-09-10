// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AppSettings.h"
#include "core/DdcClient.h"
#include "core/TouchControl.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QFile>
#include <QTimer>

#include <cstdio>
#include <cstring>

namespace {

// Options handled before QApplication is constructed, so that --version and
// --help work with no display at all (over SSH, in a build check, in a package
// test). Constructing QApplication without an X connection aborts the process,
// which is why this cannot wait until after.
//
// Qt's own command-line options are passed through untouched. Anything else
// that looks like an option is refused with usage, because silently falling
// through to the GUI is how `--version` came to look like a hang.
bool isQtOption(const char* a)
{
    static const char* kQtOpts[] = {
        "-style", "-stylesheet", "-platform", "-platformpluginpath", "-platformtheme",
        "-plugin", "-display", "-geometry", "-title", "-name", "-visual", "-ncols",
        "-cmap", "-widgetcount", "-reverse", "-session", "-qmljsdebugger",
        "-qwindowgeometry", "-qwindowtitle", "-qwindowicon", "-dograb", "-nograb",
        "-sync", "-testability",
    };
    // Accept both -style and --style, and the -style=value form.
    const char* p = a;
    while (*p == '-') ++p;
    for (const char* opt : kQtOpts) {
        const char* o = opt;
        while (*o == '-') ++o;
        const size_t n = std::strlen(o);
        if (std::strncmp(p, o, n) == 0 && (p[n] == '\0' || p[n] == '='))
            return true;
    }
    return false;
}

void printUsage(const char* argv0, std::FILE* out)
{
    std::fprintf(out,
        "usage: %s [--restore] [--version] [--help]\n"
        "\n"
        "  (no options)  launch the control window\n"
        "  --restore     reapply the saved touch mode and DDC values, then exit\n"
        "  --version     print the version and exit\n"
        "  --help        print this message and exit\n"
        "\n"
        "Device queries live in the CLI: xeneonctl [list|probe|touch]\n"
        "Qt options such as -style and -platform are also accepted.\n",
        argv0);
}

// Returns an exit code to use, or -1 to carry on into the GUI.
int handleEarlyArgs(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--version") == 0 || std::strcmp(a, "-V") == 0
            || std::strcmp(a, "-version") == 0) {
            std::printf("xeneon-ctl %s\n", XENEON_VERSION);
            return 0;
        }
        if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0
            || std::strcmp(a, "--usage") == 0) {
            printUsage(argv[0], stdout);
            return 0;
        }
        if (std::strcmp(a, "--restore") == 0)
            continue; // handled once QApplication exists
        if (a[0] == '-' && a[1] != '\0' && !isQtOption(a)) {
            std::fprintf(stderr, "%s: unknown option '%s'\n\n", argv[0], a);
            printUsage(argv[0], stderr);
            return 2;
        }
    }
    return -1;
}

// Headless login restore: reapply the saved touch mode and DDC values, then
// exit. Invoked by the autostart entry (see core/AppSettings setAutostart).
int runRestore(QCoreApplication& app)
{
    // Touch mode is applied synchronously via xinput.
    xen::TouchControl touch;
    const int mode = xen::settings::loadTouchMode(int(xen::TouchControl::Mode::MainCursor));
    touch.setMode(static_cast<xen::TouchControl::Mode>(mode));

    // DDC values are applied through the async ddcutil queue once the bus is
    // found; give the queue time to drain, then quit.
    static xen::DdcClient ddc;
    const QMap<int, int> vcps = xen::settings::loadVcps();
    QObject::connect(&ddc, &xen::DdcClient::readyChanged, &app, [&vcps](bool ready, const QString&) {
        if (!ready)
            return;
        for (auto it = vcps.constBegin(); it != vcps.constEnd(); ++it)
            ddc.setVcp(static_cast<quint8>(it.key()), static_cast<quint16>(it.value()));
    });
    ddc.start();

    // Enough for bus detect + a handful of serialized setvcp calls.
    QTimer::singleShot(vcps.isEmpty() ? 1500 : 6000, &app, &QCoreApplication::quit);
    return QCoreApplication::exec();
}

} // namespace

int main(int argc, char** argv)
{
    // Handled before QApplication so they work without a display.
    const int early = handleEarlyArgs(argc, argv);
    if (early >= 0)
        return early;

    // Qt 6.4's XCB plugin crashes in its XInput2 handler when it receives a
    // touch event from a floating device (our Indicator mode floats the Edge
    // touchscreen so it drives no pointer). We read touch ourselves on a
    // separate X connection (src/x11/TouchEventSource), so we do not need Qt's
    // own XI2 at all. Disabling it stops the crash; mouse input still works via
    // core pointer events, and touch taps on our calibration/test overlays
    // arrive as pointer-emulated clicks.
    qputenv("QT_XCB_NO_XI2", "1");

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("xeneon-ctl"));
    QApplication::setOrganizationName(QStringLiteral("xeneon-ctl"));

    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--restore") == 0)
            return runRestore(app);
    }

    QFile qss(QStringLiteral(":/theme.qss"));
    if (qss.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    xen::MainWindow w;
    w.show();
    return QApplication::exec();
}
