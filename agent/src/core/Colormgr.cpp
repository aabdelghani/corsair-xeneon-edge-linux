// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Colormgr.h"

#include <QProcess>
#include <QProcessEnvironment>

namespace xen {

std::vector<ColorDevice> readColorDevices()
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    // colormgr translates its labels and the parser reads the English ones.
    // LC_ALL outranks LANG and LC_MESSAGES, and under a C locale gettext
    // ignores LANGUAGE as well. C.UTF-8 rather than C keeps non-ASCII in
    // profile paths intact; where it does not exist, the locale falls back to
    // C, which is just as English.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    p.setProcessEnvironment(env);
    p.start(QStringLiteral("colormgr"), { QStringLiteral("get-devices-by-kind"),
                                          QStringLiteral("display") });
    if (!p.waitForStarted(1500) || !p.waitForFinished(6000))
        return {};
    if (p.exitCode() != 0)
        return {};
    return parseColorDevices(QString::fromUtf8(p.readAll()).toStdString());
}

} // namespace xen
