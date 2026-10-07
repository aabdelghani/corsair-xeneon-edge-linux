// SPDX-License-Identifier: GPL-3.0-or-later
// DdcClient writes with --noverify for speed, so a write reports success
// without anyone asking the panel what it did. On this panel the two differ:
// RGB gain is taken in percent and read back on 0..255, so a slider that wrote
// 50 kept showing 50 while the panel sat at 127 of 255. Profile apply already
// reads back for exactly this reason; a single write did not. These run
// DdcClient against a stand-in ddcutil that answers the way the Edge does.
#include "core/DdcClient.h"

#include "check.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QList>
#include <QTemporaryDir>

#include <functional>

using namespace xen;

namespace {

struct Event {
    bool read;      // vcpRead, otherwise vcpWritten
    int code;
    int value;
    int max;
};

// Spin the event loop until done() holds or five seconds pass, then a little
// longer so a job that should not exist has time to show up.
void runUntil(const std::function<bool()>& done)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < 5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    t.restart();
    while (t.elapsed() < 300)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

int reads(const QList<Event>& events, int code)
{
    int n = 0;
    for (const Event& e : events)
        n += e.read && e.code == code;
    return n;
}

const Event* lastRead(const QList<Event>& events, int code)
{
    for (auto it = events.crbegin(); it != events.crend(); ++it)
        if (it->read && it->code == code)
            return &*it;
    return nullptr;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const QByteArray fakeBin = argc > 1 ? argv[1] : "tests/fixtures/fake-ddcutil";
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    qputenv("FAKE_DDCUTIL_STATE", tmp.filePath(QStringLiteral("state")).toLocal8Bit());
    qputenv("PATH", fakeBin + ':' + qgetenv("PATH"));

    DdcClient ddc;
    QList<Event> events;
    bool ready = false;
    QObject::connect(&ddc, &DdcClient::readyChanged, [&](bool r, const QString&) { ready = r; });
    QObject::connect(&ddc, &DdcClient::vcpRead, [&](quint8 c, quint16 v, quint16 m) {
        events.append({ true, c, v, m });
    });
    QObject::connect(&ddc, &DdcClient::vcpWritten, [&](quint8 c, quint16 v) {
        events.append({ false, c, v, 0 });
    });

    ddc.start();
    runUntil([&] { return ready; });
    CHECK(ready);

    // A gain write is followed by a read of what the panel took.
    ddc.setVcp(vcp::kGainRed, 127);
    runUntil([&] { return reads(events, vcp::kGainRed) > 0; });
    const Event* red = lastRead(events, vcp::kGainRed);
    CHECK(red != nullptr);
    CHECK(red && red->value == 127);
    CHECK(red && red->max == 255);

    // Two writes to one code in quick succession: the first is already running
    // when the second arrives, so both go out, and the read comes once, after
    // the last of them.
    events.clear();
    ddc.setVcp(vcp::kGainGreen, 30);
    ddc.setVcp(vcp::kGainGreen, 40);
    runUntil([&] { return reads(events, vcp::kGainGreen) > 0; });
    CHECK(reads(events, vcp::kGainGreen) == 1);
    const Event* green = lastRead(events, vcp::kGainGreen);
    CHECK(green && green->value == 40);

    // A caller that already queued its own read (profile apply does) does not
    // get a second one.
    events.clear();
    ddc.setVcp(vcp::kBrightness, 40);
    ddc.getVcp(vcp::kBrightness);
    runUntil([&] { return reads(events, vcp::kBrightness) > 0; });
    CHECK(reads(events, vcp::kBrightness) == 1);

    // Power is a command, not a value a slider shows, so it is not read back.
    events.clear();
    ddc.setVcp(vcp::kPower, 0x05);
    runUntil([&] { return !events.isEmpty(); });
    CHECK(reads(events, vcp::kPower) == 0);

    // RGB gain goes out in percent. Callers, the cache and saved profiles hold
    // it on the 0..255 the panel reports, so the middle of the slider is the
    // middle of the range, not full gain.
    auto writeGain = [&](int value) {
        events.clear();
        ddc.setVcp(vcp::kGainBlue, quint16(value));
        runUntil([&] { return reads(events, vcp::kGainBlue) > 0; });
        const Event* e = lastRead(events, vcp::kGainBlue);
        return e ? e->value : -1;
    };
    CHECK(writeGain(127) == 127);

    // Both ends of the slider reach both ends of the range.
    CHECK(writeGain(255) == 255);
    CHECK(writeGain(0) == 0);

    // A value as the panel reported it comes back unchanged when written again,
    // which is what restoring a saved profile does. These are the read-backs
    // of 1, 25, 50, 64 and 99 percent on the real panel.
    CHECK(writeGain(2) == 2);
    CHECK(writeGain(63) == 63);
    CHECK(writeGain(127) == 127);
    CHECK(writeGain(163) == 163);
    CHECK(writeGain(252) == 252);

    // Only gain is converted. Sharpness tops out at 4 on this panel, so scaling
    // it like gain would turn 2 into 50.
    events.clear();
    ddc.getVcp(vcp::kSharpness);
    runUntil([&] { return reads(events, vcp::kSharpness) > 0; });
    events.clear();
    ddc.setVcp(vcp::kSharpness, 3);
    runUntil([&] { return reads(events, vcp::kSharpness) > 0; });
    const Event* sharpness = lastRead(events, vcp::kSharpness);
    CHECK(sharpness && sharpness->value == 3);
    CHECK(sharpness && sharpness->max == 4);

    return xen::test::report("test_ddcclient");
}
