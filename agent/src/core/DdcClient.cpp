// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DdcClient.h"

#include <QRegularExpression>

#include <algorithm>

namespace xen {
namespace {

// The picture values a slider or a profile sets and the UI shows. Power,
// input and the restore commands are commands rather than values, so a write
// to them is not followed by a read.
bool readsBack(quint8 code)
{
    switch (code) {
    case vcp::kBrightness:
    case vcp::kContrast:
    case vcp::kPreset:
    case vcp::kGainRed:
    case vcp::kGainGreen:
    case vcp::kGainBlue:
    case vcp::kSharpness:
        return true;
    default:
        return false;
    }
}

// The Edge takes RGB gain in percent but reports it on 0..255: 50 reads back
// as 127, and anything from 100 up as 255. Everything above this class - the
// cache, the sliders, saved profiles - holds the value as the panel reports
// it, so gain is converted only here, on its way out, against the maximum the
// panel reported. Rounding makes a reported value come back unchanged when it
// is written again, which is what restoring a profile does.
quint16 toWriteScale(quint8 code, quint16 value, quint16 max)
{
    if (code != vcp::kGainRed && code != vcp::kGainGreen && code != vcp::kGainBlue)
        return value;
    if (max == 0)
        max = 255;
    return quint16(std::min(100, (int(value) * 100 + max / 2) / max));
}

} // namespace

DdcClient::DdcClient(QObject* parent)
    : QObject(parent)
{
    m_proc.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) { finishJob(code); });
}

void DdcClient::start()
{
    enqueue({ Job::Detect, 0, 0 });
}

void DdcClient::fetchCapabilities()
{
    enqueue(Job{ Job::Capabilities, 0, 0 });
}

void DdcClient::getVcp(quint8 code)
{
    enqueue({ Job::Get, code, 0 });
}

void DdcClient::setVcp(quint8 code, quint16 value)
{
    // Coalesce: replace a queued (not yet running) set for the same code.
    const auto it = std::find_if(m_queue.begin(), m_queue.end(), [code](Job j) {
        return j.kind == Job::Set && j.code == code;
    });
    if (it != m_queue.end()) {
        it->value = value;
        return;
    }
    enqueue({ Job::Set, code, value });
}

void DdcClient::enqueue(Job job)
{
    m_queue.append(job);
    if (!m_running)
        startNext();
}

void DdcClient::startNext()
{
    // Re-entrancy guard. finishJob() emits signals before it starts the next
    // job, and a handler that enqueues work (the UI asking for values the
    // moment the bus is found) reaches enqueue() -> startNext() from inside
    // that emit. Without this check finishJob()'s own trailing startNext()
    // then starts a second ddcutil over the top of the first, QProcess ignores
    // it, and the running process's output gets attributed to the wrong job:
    // the capability string came back labelled as a brightness read.
    if (m_running)
        return;
    if (m_queue.isEmpty())
        return;
    m_current = m_queue.takeFirst();
    m_running = true;

    QStringList args;
    switch (m_current.kind) {
    case Job::Detect:
        args = { QStringLiteral("detect"), QStringLiteral("--brief") };
        break;
    case Job::Get:
        args = { QStringLiteral("--bus"), QString::number(m_bus),
                 QStringLiteral("--sleep-multiplier"), QStringLiteral(".4"),
                 QStringLiteral("getvcp"),
                 QString::number(m_current.code, 16) };
        break;
    case Job::Set:
        args = { QStringLiteral("--bus"), QString::number(m_bus),
                 QStringLiteral("--noverify"),
                 QStringLiteral("--sleep-multiplier"), QStringLiteral(".4"),
                 QStringLiteral("setvcp"),
                 QString::number(m_current.code, 16),
                 QString::number(toWriteScale(m_current.code, m_current.value,
                                              m_max.value(m_current.code, 255))) };
        break;
    case Job::Capabilities:
        args = { QStringLiteral("--bus"), QString::number(m_bus),
                 QStringLiteral("capabilities") };
        break;
    }
    m_proc.start(QStringLiteral("ddcutil"), args);
}

void DdcClient::finishJob(int exitCode)
{
    const QString out = QString::fromUtf8(m_proc.readAll());
    const Job job = m_current;
    m_running = false;

    switch (job.kind) {
    case Job::Detect: {
        // Find the display block naming the Edge and grab its /dev/i2c-N.
        int newBus = -1;
        const QStringList blocks = out.split(QStringLiteral("Display "));
        for (const QString& b : blocks) {
            if (b.contains(QStringLiteral("XENEON EDGE"))) {
                static const QRegularExpression re(QStringLiteral("/dev/i2c-(\\d+)"));
                const auto m = re.match(b);
                if (m.hasMatch())
                    newBus = m.captured(1).toInt();
            }
        }
        m_bus = newBus;
        emit readyChanged(m_bus >= 0,
                          m_bus >= 0
                              ? tr("Edge found on i2c bus %1").arg(m_bus)
                              : tr("Edge not found by ddcutil (is it connected via DisplayPort/HDMI?)"));
        break;
    }
    case Job::Get: {
        if (exitCode == 0) {
            // Continuous: "current value =    95, max value =   100"
            // Non-continuous (ddcutil 1.4): "...): User 1 (0x0b), Tolerance..."
            //                or older style: "... (sl=0x05)"
            static const QRegularExpression cont(
                QStringLiteral("current value\\s*=\\s*(\\d+),\\s*max value\\s*=\\s*(\\d+)"));
            static const QRegularExpression nc(
                QStringLiteral("(?:sl=0x|\\(0x)([0-9a-fA-F]+)\\)?"));
            if (auto m = cont.match(out); m.hasMatch()) {
                if (const quint16 max = m.captured(2).toUShort(); max > 0)
                    m_max[job.code] = max;
                emit vcpRead(job.code, m.captured(1).toUShort(), m.captured(2).toUShort());
            } else if (auto n = nc.match(out); n.hasMatch()) {
                emit vcpRead(job.code, n.captured(1).toUShort(nullptr, 16), 0);
            } else {
                emit errorOccurred(tr("getvcp %1: unparsed output: %2")
                                       .arg(job.code, 0, 16).arg(out.trimmed()));
            }
        } else {
            emit errorOccurred(tr("getvcp 0x%1 failed: %2").arg(job.code, 0, 16).arg(out.trimmed()));
        }
        break;
    }
    case Job::Capabilities: {
        if (exitCode == 0)
            emit capabilitiesRead(out);
        else
            emit errorOccurred(tr("capabilities failed: %1").arg(out.trimmed()));
        break;
    }
    case Job::Set: {
        if (exitCode == 0) {
            emit vcpWritten(job.code, job.value);
            // --noverify keeps writes fast, so ask the panel what it made of
            // this one rather than trusting it: on this panel a gain written
            // as 50 reads back as 127 of 255. Skipped while another write or
            // a read of the same code is still queued, since that one settles
            // it anyway.
            const bool settledLater = std::any_of(m_queue.begin(), m_queue.end(), [&job](Job j) {
                return (j.kind == Job::Set || j.kind == Job::Get) && j.code == job.code;
            });
            if (readsBack(job.code) && !settledLater)
                m_queue.append({ Job::Get, job.code, 0 });
        } else {
            emit errorOccurred(tr("setvcp 0x%1 failed: %2").arg(job.code, 0, 16).arg(out.trimmed()));
        }
        break;
    }
    }
    startNext();
}

} // namespace xen
