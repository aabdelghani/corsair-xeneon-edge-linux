// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Colormgr.h"

#include <QProcess>

namespace xen {

std::vector<ColorDevice> readColorDevices()
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(QStringLiteral("colormgr"), { QStringLiteral("get-devices-by-kind"),
                                          QStringLiteral("display") });
    if (!p.waitForStarted(1500) || !p.waitForFinished(6000))
        return {};
    if (p.exitCode() != 0)
        return {};
    return parseColorDevices(QString::fromUtf8(p.readAll()).toStdString());
}

} // namespace xen
