// edgeline: five-point touch calibration solve.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Lifted out of the old Qt CalibrationOverlay so the maths can be tested and
// so the UI that collects the taps (now Electron) does not have to know any of
// it. This header is deliberately free of Qt and of X11: it takes numbers and
// returns numbers.
//
// The problem: X applies a "Coordinate Transformation Matrix" to the digitizer
// before anything sees a touch. So a tap tells you where the pointer LANDED,
// not where the finger was. To correct the panel you must first undo the matrix
// that was in effect while measuring, recover the raw device coordinate, and
// only then fit target against raw.
#pragma once

#include <array>
#include <string>
#include <vector>

namespace xen {

struct CalPoint {
    double x = 0;
    double y = 0;
};

// A 3x3 row-major affine, the same shape xinput's Coordinate Transformation
// Matrix takes. Maps device-normalised coordinates to screen-normalised ones.
using Matrix3 = std::array<double, 9>;

inline constexpr Matrix3 kIdentity{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };

struct CalibrationInput {
    // Target and measured positions, in pixels local to the Edge screen, in
    // matching order. Five pairs is the intended count; three is the minimum
    // for an affine fit and more than five is accepted.
    std::vector<CalPoint> targets;
    std::vector<CalPoint> measured;

    // The virtual desktop the matrix maps into, and where the Edge sits inside
    // it. The matrix spans every monitor, not just this one, which is the
    // single most common way to get this calculation wrong.
    double virtualWidth = 0;
    double virtualHeight = 0;
    double originX = 0;
    double originY = 0;

    // The matrix that was in effect while the taps were measured.
    Matrix3 base = kIdentity;
};

struct CalibrationResult {
    bool ok = false;
    std::string error;
    Matrix3 matrix = kIdentity;

    // How far each corrected point still lands from its target, in Edge
    // pixels, plus the RMS across all points. Reported so a bad run can be
    // rejected before anything is written to the device.
    std::vector<double> residualsPx;
    double rmsPx = 0;
    double worstPx = 0;
};

// Pure. Writes nothing, touches no device.
CalibrationResult solveCalibration(const CalibrationInput& in);

} // namespace xen
