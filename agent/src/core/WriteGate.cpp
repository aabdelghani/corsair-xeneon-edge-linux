// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/WriteGate.h"

#include "transport/HidTransport.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

namespace xen {

namespace {

QString hex(const std::vector<uint8_t>& b)
{
    QString s;
    for (uint8_t const x : b)
        s += QString::asprintf("%02X ", x);
    return s.trimmed();
}

} // namespace

QString WriteGate::logFilePath()
{
    const QString dir = QDir::homePath() + QStringLiteral("/.local/share/edgeline");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/hid.log");
}

void WriteGate::append(const QString& line)
{
    QFile f(logFilePath());
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << QDateTime::currentDateTime().toString(Qt::ISODate) << "  " << line << '\n';
    }
}

Exchange WriteGate::exchange(const QString& hidrawPath, const BragiFrame& frame,
                             const Confirmation& conf, int readTimeoutMs)
{
    Exchange ex;
    ex.logPath = logFilePath();
    ex.tx.assign(frame.data(), frame.data() + xen::BragiFrame::size());

    if (conf.reason().isEmpty()) {
        ex.error = QStringLiteral("refused: no confirmation");
        append(QStringLiteral("REFUSED (no confirmation) TX=%1").arg(hex(ex.tx)));
        return ex;
    }

    HidTransport t;
    if (!t.open(hidrawPath.toStdString())) {
        ex.error = QStringLiteral("open failed: %1").arg(QString::fromStdString(t.lastError()));
        append(QStringLiteral("OPEN-FAIL %1 (%2)").arg(hidrawPath, ex.error));
        return ex;
    }

    append(QStringLiteral("TX [%1]  %2").arg(conf.reason(), hex(ex.tx)));
    const int n = t.write(frame.data(), xen::BragiFrame::size());
    if (n < 0) {
        ex.error = QStringLiteral("write failed: %1").arg(QString::fromStdString(t.lastError()));
        append(QStringLiteral("TX-FAIL %1").arg(ex.error));
        return ex;
    }

    ex.rx = t.read(64, readTimeoutMs);
    if (ex.rx.empty()) {
        append(QStringLiteral("RX (none within %1 ms)").arg(readTimeoutMs));
    } else {
        append(QStringLiteral("RX        %1").arg(hex(ex.rx)));
        ex.ok = true;
    }
    return ex;
}

} // namespace xen
