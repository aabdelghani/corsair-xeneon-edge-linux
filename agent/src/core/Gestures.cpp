// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Gestures.h"

#include <algorithm>
#include <cmath>

namespace xen {
namespace {

const char* baseName(GestureKind k)
{
    switch (k) {
    case GestureKind::SwipeLeft:  return "swipe-left";
    case GestureKind::SwipeRight: return "swipe-right";
    case GestureKind::SwipeUp:    return "swipe-up";
    case GestureKind::SwipeDown:  return "swipe-down";
    case GestureKind::Tap:        return "tap";
    case GestureKind::LongPress:  return "long-press";
    case GestureKind::PinchIn:    return "pinch-in";
    case GestureKind::PinchOut:   return "pinch-out";
    case GestureKind::None:       break;
    }
    return "none";
}

const char* fingerPrefix(int contacts)
{
    switch (contacts) {
    case 2: return "two-finger-";
    case 3: return "three-finger-";
    case 4: return "four-finger-";
    default: return "";
    }
}

double distance(double ax, double ay, double bx, double by)
{
    const double dx = ax - bx;
    const double dy = ay - by;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

std::string Gesture::name() const
{
    if (kind == GestureKind::None)
        return "none";
    // A pinch is inherently two-fingered; naming it "two-finger-pinch-in" adds
    // nothing.
    if (kind == GestureKind::PinchIn || kind == GestureKind::PinchOut)
        return baseName(kind);
    return std::string(fingerPrefix(contacts)) + baseName(kind);
}

GestureRecognizer::GestureRecognizer(GestureLimits limits)
    : m_limits(limits)
{
}

void GestureRecognizer::reset()
{
    m_active.clear();
    m_finished.clear();
    m_peakContacts = 0;
}

GestureRecognizer::Contact* GestureRecognizer::find(int id)
{
    const auto it = std::find_if(m_active.begin(), m_active.end(),
                                 [id](const Contact& c) { return c.id == id; });
    return it == m_active.end() ? nullptr : &*it;
}

void GestureRecognizer::begin(int id, double nx, double ny, int64_t tMs)
{
    // A repeated begin for a live id means we missed an end. Treat it as a
    // fresh contact rather than accumulating a duplicate.
    if (Contact* existing = find(id)) {
        existing->startX = nx * m_limits.panelWidthPx;
        existing->startY = ny * m_limits.panelHeightPx;
        existing->lastX = existing->startX;
        existing->lastY = existing->startY;
        existing->startMs = tMs;
        return;
    }
    Contact c;
    c.id = id;
    c.startX = c.lastX = nx * m_limits.panelWidthPx;
    c.startY = c.lastY = ny * m_limits.panelHeightPx;
    c.startMs = tMs;
    m_active.push_back(c);
    m_peakContacts = std::max(m_peakContacts, int(m_active.size()));
}

void GestureRecognizer::update(int id, double nx, double ny, int64_t)
{
    if (Contact* c = find(id)) {
        c->lastX = nx * m_limits.panelWidthPx;
        c->lastY = ny * m_limits.panelHeightPx;
    }
}

Gesture GestureRecognizer::end(int id, int64_t tMs)
{
    const auto it = std::find_if(m_active.begin(), m_active.end(),
                                 [id](const Contact& c) { return c.id == id; });
    if (it == m_active.end())
        return {};

    Contact done = *it;
    done.live = false;
    m_active.erase(it);
    m_finished.push_back(done);

    // Only classify once every finger is off the panel, so a two-finger swipe
    // is one gesture rather than two arriving a few milliseconds apart.
    if (!m_active.empty())
        return {};

    const Gesture g = classify(tMs);
    reset();
    return g;
}

Gesture GestureRecognizer::classify(int64_t endMs) const
{
    if (m_finished.empty())
        return {};

    Gesture g;
    g.contacts = m_peakContacts;

    int64_t startMs = m_finished.front().startMs;
    for (const Contact& c : m_finished)
        startMs = std::min(startMs, c.startMs);
    g.durationMs = endMs - startMs;

    // Pinch first: two contacts whose separation changed substantially is a
    // pinch even if each finger also travelled far enough to look like a swipe.
    if (m_finished.size() == 2) {
        const Contact& a = m_finished[0];
        const Contact& b = m_finished[1];
        const double before = distance(a.startX, a.startY, b.startX, b.startY);
        const double after = distance(a.lastX, a.lastY, b.lastX, b.lastY);
        const double change = after - before;
        if (std::abs(change) >= m_limits.pinchMinPx) {
            g.kind = change > 0 ? GestureKind::PinchOut : GestureKind::PinchIn;
            g.distancePx = std::abs(change);
            return g;
        }
    }

    // Otherwise use the average travel of every contact, so a two-finger swipe
    // is judged by where the hand went rather than by its most eager finger.
    double sumDx = 0;
    double sumDy = 0;
    for (const Contact& c : m_finished) {
        sumDx += c.lastX - c.startX;
        sumDy += c.lastY - c.startY;
    }
    const double dx = sumDx / double(m_finished.size());
    const double dy = sumDy / double(m_finished.size());
    const double adx = std::abs(dx);
    const double ady = std::abs(dy);
    const double travel = std::sqrt(dx * dx + dy * dy);

    if (travel >= m_limits.swipeMinPx && g.durationMs <= m_limits.swipeMaxMs) {
        // Require one axis to dominate, so a diagonal drag is not forced into
        // a direction it only barely favours.
        if (adx >= ady * m_limits.swipeAxisRatio) {
            g.kind = dx < 0 ? GestureKind::SwipeLeft : GestureKind::SwipeRight;
            g.distancePx = adx;
            return g;
        }
        if (ady >= adx * m_limits.swipeAxisRatio) {
            g.kind = dy < 0 ? GestureKind::SwipeUp : GestureKind::SwipeDown;
            g.distancePx = ady;
            return g;
        }
        return {};   // moved a long way, but in no particular direction
    }

    if (travel <= m_limits.tapMaxPx) {
        if (g.durationMs >= m_limits.longPressMs) {
            g.kind = GestureKind::LongPress;
            return g;
        }
        if (g.durationMs <= m_limits.tapMaxMs) {
            g.kind = GestureKind::Tap;
            return g;
        }
    }
    return {};   // a slow short drag is not a gesture, it is a fidget
}

} // namespace xen
