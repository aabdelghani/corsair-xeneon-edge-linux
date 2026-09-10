// edgeline: recognise swipes, taps and pinches from raw touch.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pure. It takes contact positions and timestamps and returns a gesture, with
// no X11, no devices and no clock of its own, so the thresholds can be tested
// against exact inputs instead of by waving a hand at a panel.
//
// Positions arrive normalised (0..1) and are converted to panel pixels before
// anything is measured. The Edge is 2560x720, so a distance that is a small
// fraction of the width is a large fraction of the height: thresholds in
// normalised units would make a vertical swipe far easier than a horizontal
// one, which is not what a finger does.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xen {

enum class GestureKind {
    None,
    SwipeLeft,
    SwipeRight,
    SwipeUp,
    SwipeDown,
    Tap,
    LongPress,
    PinchIn,
    PinchOut,
};

struct Gesture {
    GestureKind kind = GestureKind::None;
    int contacts = 0;        // how many fingers took part
    double distancePx = 0;   // travel for a swipe, or scale change for a pinch
    int64_t durationMs = 0;

    [[nodiscard]] explicit operator bool() const { return kind != GestureKind::None; }
    [[nodiscard]] std::string name() const;   // "swipe-left", "two-finger-tap", ...
};

struct GestureLimits {
    double panelWidthPx = 2560;
    double panelHeightPx = 720;

    // A swipe has to travel, be mostly along one axis, and not take all day.
    double swipeMinPx = 180;
    double swipeAxisRatio = 1.6;   // dominant axis over the other
    int64_t swipeMaxMs = 900;

    // A tap barely moves. Anything slower than longPressMs that also barely
    // moved is a long press instead.
    double tapMaxPx = 26;
    int64_t tapMaxMs = 320;
    int64_t longPressMs = 600;

    // A pinch needs two contacts and a real change in their separation.
    double pinchMinPx = 120;
};

// Fed one contact at a time. A gesture is produced when the last contact of a
// sequence lifts, so a two-finger swipe is one gesture and not two.
class GestureRecognizer {
public:
    explicit GestureRecognizer(GestureLimits limits = {});

    void begin(int id, double nx, double ny, int64_t tMs);
    void update(int id, double nx, double ny, int64_t tMs);
    // Returns a gesture only when this was the last contact down.
    Gesture end(int id, int64_t tMs);

    void reset();
    [[nodiscard]] int activeContacts() const { return int(m_active.size()); }

private:
    struct Contact {
        int id = 0;
        double startX = 0, startY = 0;   // panel pixels
        double lastX = 0, lastY = 0;
        int64_t startMs = 0;
        bool live = true;
    };

    Contact* find(int id);
    Gesture classify(int64_t endMs) const;

    GestureLimits m_limits;
    std::vector<Contact> m_active;    // contacts currently down
    std::vector<Contact> m_finished;  // lifted, awaiting the last release
    int m_peakContacts = 0;
};

} // namespace xen
