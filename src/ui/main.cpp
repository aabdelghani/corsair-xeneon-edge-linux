// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/MainWindow.h"

#include <QApplication>
#include <QFile>

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

    QFile qss(QStringLiteral(":/theme.qss"));
    if (qss.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    xen::MainWindow w;
    w.show();
    return app.exec();
}
