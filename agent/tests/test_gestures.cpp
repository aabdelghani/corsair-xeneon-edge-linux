// SPDX-License-Identifier: GPL-3.0-or-later
// The recogniser turns finger movement into actions that change the panel, so
// its thresholds decide whether a deliberate swipe is missed or a stray brush
// switches your profile. Tested against exact inputs rather than by waving at
// the glass.
#include "core/Gestures.h"

#include "check.h"

#include <cstdio>

using namespace xen;

namespace {

// Normalised helpers for a 2560x720 panel: it is far wider than it is tall, so
// the same normalised distance means very different things per axis.
constexpr double nx(double px) { return px / 2560.0; }
constexpr double ny(double px) { return px / 720.0; }

// One finger, straight line, given duration.
Gesture oneFinger(double x0, double y0, double x1, double y1, int64_t ms)
{
    GestureRecognizer r;
    r.begin(1, nx(x0), ny(y0), 0);
    r.update(1, nx((x0 + x1) / 2), ny((y0 + y1) / 2), ms / 2);
    r.update(1, nx(x1), ny(y1), ms);
    return r.end(1, ms);
}

// Two fingers moving together.
Gesture twoFinger(double dx, double dy, int64_t ms)
{
    GestureRecognizer r;
    r.begin(1, nx(800), ny(300), 0);
    r.begin(2, nx(900), ny(300), 5);
    r.update(1, nx(800 + dx), ny(300 + dy), ms);
    r.update(2, nx(900 + dx), ny(300 + dy), ms);
    r.end(1, ms);
    return r.end(2, ms);
}

} // namespace

int main()
{
    // ------------------------------------------------------------ swipes

    CHECK(oneFinger(1200, 360, 600, 360, 300).kind == GestureKind::SwipeLeft);
    CHECK(oneFinger(600, 360, 1200, 360, 300).kind == GestureKind::SwipeRight);
    CHECK(oneFinger(1200, 600, 1200, 150, 300).kind == GestureKind::SwipeUp);
    CHECK(oneFinger(1200, 150, 1200, 600, 300).kind == GestureKind::SwipeDown);

    // A vertical swipe must be possible at all on a 720px-tall panel. If the
    // thresholds were in normalised units this would need most of the height.
    {
        const auto g = oneFinger(1200, 550, 1200, 300, 250);
        CHECK(g.kind == GestureKind::SwipeUp);
        CHECK(g.distancePx > 200);
    }

    // Too short to be deliberate.
    CHECK(oneFinger(1200, 360, 1100, 360, 200).kind != GestureKind::SwipeLeft);
    // Long enough but too slow: a drag, not a swipe.
    CHECK(oneFinger(1200, 360, 600, 360, 2000).kind == GestureKind::None);
    // Diagonal, favouring neither axis clearly enough to guess.
    CHECK(oneFinger(600, 200, 1100, 560, 300).kind == GestureKind::None);

    // ------------------------------------------------------------ taps

    CHECK(oneFinger(1200, 360, 1205, 362, 90).kind == GestureKind::Tap);
    CHECK(oneFinger(1200, 360, 1205, 362, 900).kind == GestureKind::LongPress);
    // Between a tap and a long press, moving barely at all, is neither.
    CHECK(oneFinger(1200, 360, 1205, 362, 450).kind == GestureKind::None);

    // ------------------------------------------------------------ fingers

    {
        const auto g = twoFinger(-500, 0, 300);
        CHECK(g.kind == GestureKind::SwipeLeft);
        CHECK(g.contacts == 2);
        CHECK(g.name() == "two-finger-swipe-left");
    }
    {
        GestureRecognizer r;
        r.begin(1, nx(800), ny(300), 0);
        r.begin(2, nx(900), ny(300), 5);
        r.begin(3, nx(1000), ny(300), 8);
        r.end(1, 100); r.end(2, 100);
        const auto g = r.end(3, 100);
        CHECK(g.kind == GestureKind::Tap);
        CHECK(g.name() == "three-finger-tap");
    }

    // A gesture is reported once, when the last finger lifts, not per finger.
    {
        GestureRecognizer r;
        r.begin(1, nx(1200), ny(300), 0);
        r.begin(2, nx(1300), ny(300), 5);
        r.update(1, nx(700), ny(300), 200);
        r.update(2, nx(800), ny(300), 200);
        CHECK(!bool(r.end(1, 220)));      // first lift says nothing
        CHECK(bool(r.end(2, 220)));       // second produces the gesture
        CHECK(r.activeContacts() == 0);
    }

    // ------------------------------------------------------------ pinch

    {
        // Two fingers moving apart is a pinch out, even though each finger
        // travels far enough to look like a swipe on its own.
        GestureRecognizer r;
        r.begin(1, nx(1100), ny(360), 0);
        r.begin(2, nx(1300), ny(360), 5);
        r.update(1, nx(900), ny(360), 300);
        r.update(2, nx(1500), ny(360), 300);
        r.end(1, 320);
        const auto g = r.end(2, 320);
        CHECK(g.kind == GestureKind::PinchOut);
        CHECK(g.name() == "pinch-out");   // not "two-finger-pinch-out"
    }
    {
        GestureRecognizer r;
        r.begin(1, nx(900), ny(360), 0);
        r.begin(2, nx(1500), ny(360), 5);
        r.update(1, nx(1150), ny(360), 300);
        r.update(2, nx(1250), ny(360), 300);
        r.end(1, 320);
        CHECK(r.end(2, 320).kind == GestureKind::PinchIn);
    }
    // Two fingers moving together is a swipe, not a pinch: their separation
    // barely changes.
    CHECK(twoFinger(-500, 0, 300).kind == GestureKind::SwipeLeft);

    // ------------------------------------------------------------ robustness

    {
        // An end for a contact that never began must not invent a gesture.
        GestureRecognizer r;
        CHECK(!bool(r.end(99, 100)));
    }
    {
        // A duplicate begin (a missed end) restarts that contact rather than
        // accumulating a phantom one.
        GestureRecognizer r;
        r.begin(1, nx(100), ny(300), 0);
        r.begin(1, nx(1200), ny(300), 10);
        r.update(1, nx(600), ny(300), 300);
        const auto g = r.end(1, 320);
        CHECK(g.contacts == 1);
        CHECK(g.kind == GestureKind::SwipeLeft);
    }
    {
        GestureRecognizer r;
        r.begin(1, nx(1200), ny(300), 0);
        r.reset();
        CHECK(r.activeContacts() == 0);
        CHECK(!bool(r.end(1, 100)));
    }

    CHECK(Gesture{}.name() == "none");
    CHECK(!bool(Gesture{}));

    return xen::test::report("test_gestures");
}
