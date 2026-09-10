// SPDX-License-Identifier: GPL-3.0-or-later
// The calibration solve decides where every future touch lands. A silent error
// here is not a crash, it is a panel that is subtly wrong forever, so the maths
// is pinned down here rather than trusted.
#include "core/Calibration.h"

#include "check.h"

#include <cmath>
#include <cstdio>

using xen::CalibrationInput;
using xen::CalPoint;
using xen::solveCalibration;

namespace {

// The Edge sits below a 3440x1440 main monitor, which is the real topology on
// this machine and the case the virtual-desktop normalisation exists for.
CalibrationInput baseCase()
{
    CalibrationInput in;
    in.virtualWidth = 3440;
    in.virtualHeight = 2160;
    in.originX = 0;
    in.originY = 1440;
    in.base = xen::kIdentity;
    // The five targets the overlay draws, as Edge-local pixels on a 2560x720.
    in.targets = { { 307.2, 129.6 }, { 2252.8, 129.6 }, { 1280.0, 360.0 },
                   { 307.2, 590.4 }, { 2252.8, 590.4 } };
    return in;
}

bool near(double a, double b, double eps) { return std::abs(a - b) < eps; }

} // namespace

int main()
{
    // A perfect run: every tap lands exactly on its target. The correction
    // should be the identity, and nothing should be left over.
    {
        CalibrationInput in = baseCase();
        in.measured = in.targets;
        const auto r = solveCalibration(in);
        CHECK(r.ok);
        CHECK(r.rmsPx < 1e-6);
        CHECK(r.worstPx < 1e-6);
        CHECK(r.residualsPx.size() == in.targets.size());
        CHECK(near(r.matrix[0], 1.0, 1e-9));
        CHECK(near(r.matrix[1], 0.0, 1e-9));
        CHECK(near(r.matrix[2], 0.0, 1e-9));
        CHECK(near(r.matrix[4], 1.0, 1e-9));
        CHECK(near(r.matrix[8], 1.0, 1e-9));
    }

    // A panel offset: every tap lands 40px right and 12px low of the target.
    // The solve must fit it exactly, because a constant offset is affine.
    {
        CalibrationInput in = baseCase();
        for (const auto& t : in.targets)
            in.measured.push_back({ t.x + 40.0, t.y + 12.0 });
        const auto r = solveCalibration(in);
        CHECK(r.ok);
        CHECK(r.rmsPx < 1e-6);
        // Correcting a +40px x error means translating back by 40 Edge pixels,
        // expressed in the virtual desktop's normalised units.
        CHECK(near(r.matrix[2] * in.virtualWidth, -40.0, 1e-6));
        CHECK(near(r.matrix[5] * in.virtualHeight, -12.0, 1e-6));
    }

    // A scale error, which is the case a corner-only fit would get wrong.
    {
        CalibrationInput in = baseCase();
        for (const auto& t : in.targets)
            in.measured.push_back({ t.x * 1.05, t.y * 0.97 });
        const auto r = solveCalibration(in);
        CHECK(r.ok);
        CHECK(r.rmsPx < 1e-6);
        CHECK(near(r.matrix[0], 1.0 / 1.05, 1e-6));
        CHECK(near(r.matrix[4], 1.0 / 0.97, 1e-6));
    }

    // A non-identity base matrix must be undone before fitting. If the base is
    // ignored the correction gets applied twice and the panel gets worse.
    {
        CalibrationInput in = baseCase();
        in.base = { 0.5, 0, 0.25, 0, 0.5, 0.25, 0, 0, 1 };
        in.measured = in.targets;
        const auto r = solveCalibration(in);
        CHECK(r.ok);
        CHECK(r.rmsPx < 1e-6);
        // Taps already landing on target means the base matrix was already
        // correct, so the solve must reproduce it rather than return identity.
        CHECK(near(r.matrix[0], 0.5, 1e-9));
        CHECK(near(r.matrix[2], 0.25, 1e-9));
    }

    // Noise: a sloppy run must still solve, and must report the error honestly
    // rather than claiming a clean fit.
    {
        CalibrationInput in = baseCase();
        const double jitter[5][2] = { { 3, -2 }, { -4, 1 }, { 2, 2 }, { -1, -3 }, { 5, 4 } };
        for (size_t i = 0; i < in.targets.size(); ++i)
            in.measured.push_back({ in.targets[i].x + jitter[i][0], in.targets[i].y + jitter[i][1] });
        const auto r = solveCalibration(in);
        CHECK(r.ok);
        CHECK(r.rmsPx > 0.5);      // it did not pretend to be perfect
        CHECK(r.rmsPx < 10.0);     // but it did fit
        CHECK(r.worstPx >= r.rmsPx);
    }

    // Degenerate inputs must fail loudly instead of writing a broken matrix.
    {
        CalibrationInput in = baseCase();
        in.targets.resize(2);
        in.measured = in.targets;
        CHECK(!solveCalibration(in).ok);
    }
    {
        CalibrationInput in = baseCase();
        in.measured = in.targets;
        in.measured.pop_back();     // mismatched counts
        CHECK(!solveCalibration(in).ok);
    }
    {
        CalibrationInput in = baseCase();
        in.measured = in.targets;
        in.base = { 0, 0, 0, 0, 0, 0, 0, 0, 1 };   // singular base
        const auto r = solveCalibration(in);
        CHECK(!r.ok);
        CHECK(!r.error.empty());
    }
    {
        // All five taps on one line: an affine fit is underdetermined.
        CalibrationInput in = baseCase();
        in.targets = { { 100, 360 }, { 600, 360 }, { 1100, 360 }, { 1600, 360 }, { 2100, 360 } };
        in.measured = in.targets;
        CHECK(!solveCalibration(in).ok);
    }
    {
        CalibrationInput in = baseCase();
        in.measured = in.targets;
        in.virtualWidth = 0;
        CHECK(!solveCalibration(in).ok);
    }

    return xen::test::report("test_calibration");
}
