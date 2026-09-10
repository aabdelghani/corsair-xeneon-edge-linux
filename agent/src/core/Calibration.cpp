// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Calibration.h"

#include <cmath>

namespace xen {
namespace {

// Solve A x = b for a 3x3 system by Gauss-Jordan with partial pivoting.
// False if singular.
bool solve3(double A[3][3], double b[3], double x[3])
{
    for (int col = 0; col < 3; ++col) {
        int piv = col;
        for (int r = col + 1; r < 3; ++r)
            if (std::abs(A[r][col]) > std::abs(A[piv][col]))
                piv = r;
        if (std::abs(A[piv][col]) < 1e-12)
            return false;
        if (piv != col) {
            for (int c = 0; c < 3; ++c)
                std::swap(A[piv][c], A[col][c]);
            std::swap(b[piv], b[col]);
        }
        for (int r = 0; r < 3; ++r) {
            if (r == col)
                continue;
            const double f = A[r][col] / A[col][col];
            for (int c = 0; c < 3; ++c)
                A[r][c] -= f * A[col][c];
            b[r] -= f * b[col];
        }
    }
    for (int i = 0; i < 3; ++i)
        x[i] = b[i] / A[i][i];
    return true;
}

// Least-squares affine row: find (a,b,c) minimising sum (a*px + b*py + c - q)^2.
// Built as normal equations over the basis [px, py, 1].
bool fitAffineRow(const std::vector<CalPoint>& src, const std::vector<double>& q, double out[3])
{
    double A[3][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
    double rhs[3] = { 0, 0, 0 };
    for (size_t i = 0; i < src.size(); ++i) {
        const double basis[3] = { src[i].x, src[i].y, 1.0 };
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c)
                A[r][c] += basis[r] * basis[c];
            rhs[r] += basis[r] * q[i];
        }
    }
    return solve3(A, rhs, out);
}

} // namespace

CalibrationResult solveCalibration(const CalibrationInput& in)
{
    CalibrationResult res;

    const size_t n = in.targets.size();
    if (n < 3 || in.measured.size() != n) {
        res.error = "need at least three matching target and measured points";
        return res;
    }
    if (in.virtualWidth <= 0 || in.virtualHeight <= 0) {
        res.error = "virtual desktop size is unknown";
        return res;
    }

    // Invert the base affine so a measured landing point can be turned back
    // into the raw device coordinate that produced it.
    const double a = in.base[0];
    const double b = in.base[1];
    const double c = in.base[2];
    const double d = in.base[3];
    const double e = in.base[4];
    const double f = in.base[5];
    const double det = a * e - b * d;
    if (std::abs(det) < 1e-12) {
        res.error = "the matrix in effect during measurement is degenerate";
        return res;
    }

    const double W = in.virtualWidth;
    const double H = in.virtualHeight;

    std::vector<CalPoint> raw;   // device-normalised coords behind each tap
    std::vector<double> tx;      // desired screen-normalised targets
    std::vector<double> ty;
    raw.reserve(n);
    tx.reserve(n);
    ty.reserve(n);

    for (size_t i = 0; i < n; ++i) {
        const double pnx = (in.measured[i].x + in.originX) / W;
        const double pny = (in.measured[i].y + in.originY) / H;
        CalPoint dev;
        dev.x = (e * (pnx - c) - b * (pny - f)) / det;
        dev.y = (-d * (pnx - c) + a * (pny - f)) / det;
        raw.push_back(dev);
        tx.push_back((in.targets[i].x + in.originX) / W);
        ty.push_back((in.targets[i].y + in.originY) / H);
    }

    double rowX[3];
    double rowY[3];
    if (!fitAffineRow(raw, tx, rowX) || !fitAffineRow(raw, ty, rowY)) {
        res.error = "could not solve: the tapped points are collinear or coincident";
        return res;
    }

    res.matrix = { rowX[0], rowX[1], rowX[2], rowY[0], rowY[1], rowY[2], 0, 0, 1 };

    // Residual: push each raw point back through the solved matrix and measure
    // how far it lands from its target, in Edge pixels rather than normalised
    // units so the number means something to a person.
    double sumSq = 0;
    res.residualsPx.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const double fx = rowX[0] * raw[i].x + rowX[1] * raw[i].y + rowX[2];
        const double fy = rowY[0] * raw[i].x + rowY[1] * raw[i].y + rowY[2];
        const double dxPx = (fx - tx[i]) * W;
        const double dyPx = (fy - ty[i]) * H;
        const double distPx = std::sqrt(dxPx * dxPx + dyPx * dyPx);
        res.residualsPx.push_back(distPx);
        sumSq += distPx * distPx;
        if (distPx > res.worstPx)
            res.worstPx = distPx;
    }
    res.rmsPx = std::sqrt(sumSq / static_cast<double>(n));
    res.ok = true;
    return res;
}

} // namespace xen
