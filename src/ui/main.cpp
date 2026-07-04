// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AppSettings.h"
#include "core/DdcClient.h"
#include "core/TouchControl.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QFile>
#include <QTimer>

namespace {

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
    return app.exec();
}

} // namespace

int main(int argc, char** argv)
{
    // Qt 6.4's XCB plugin crashes in its XInput2 handler when it receives a
    // touch event from a floating device (our Indicator mode floats the Edge
    // touchscreen so it drives no pointer). We read touch ourselves on a
    // separate X connection (src/x11/TouchEventSource), so we do not need Qt's
    // own XI2 at all. Disabling it stops the crash; mouse input still works via
    // core pointer events, and touch taps on our calibration/test overlays
    // arrive as pointer-emulated clicks.
    qputenv("QT_XCB_NO_XI2", "1");

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("xeneon-ctl"));
    app.setOrganizationName(QStringLiteral("xeneon-ctl"));

    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--restore") == 0)
            return runRestore(app);
    }

    QFile qss(QStringLiteral(":/theme.qss"));
    if (qss.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    xen::MainWindow w;
    w.show();
    return app.exec();
}
