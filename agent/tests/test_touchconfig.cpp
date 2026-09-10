// SPDX-License-Identifier: GPL-3.0-or-later
// The lock zone decides which touches are thrown away. Getting the geometry
// wrong either ignores the whole panel or ignores nothing, and both look like
// the feature simply not working.
#include "core/TouchConfig.h"

#include "check.h"

#include <QCoreApplication>

#include <cstdio>

using namespace xen::touchcfg;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // Disabled means nothing is ever in the zone, whatever the geometry says.
    {
        LockZone z;
        z.enabled = false;
        z.side = QStringLiteral("right");
        z.fraction = 0.9;
        CHECK(!inLockZone(z, 0.99, 0.5));
    }

    // Right-hand band: the right 40% is reserved, the rest is not.
    {
        LockZone z;
        z.enabled = true;
        z.side = QStringLiteral("right");
        z.fraction = 0.4;
        CHECK(inLockZone(z, 0.95, 0.5));
        CHECK(inLockZone(z, 0.60, 0.5));   // exactly on the boundary
        CHECK(!inLockZone(z, 0.59, 0.5));
        CHECK(!inLockZone(z, 0.05, 0.5));
        // The band spans the full height: a palm anywhere in it is ignored.
        CHECK(inLockZone(z, 0.95, 0.01));
        CHECK(inLockZone(z, 0.95, 0.99));
    }

    {
        LockZone z;
        z.enabled = true;
        z.side = QStringLiteral("left");
        z.fraction = 0.25;
        CHECK(inLockZone(z, 0.10, 0.5));
        CHECK(inLockZone(z, 0.25, 0.5));
        CHECK(!inLockZone(z, 0.26, 0.5));
    }
    {
        LockZone z;
        z.enabled = true;
        z.side = QStringLiteral("bottom");
        z.fraction = 0.3;
        CHECK(inLockZone(z, 0.5, 0.85));
        CHECK(!inLockZone(z, 0.5, 0.5));
    }
    {
        LockZone z;
        z.enabled = true;
        z.side = QStringLiteral("top");
        z.fraction = 0.3;
        CHECK(inLockZone(z, 0.5, 0.15));
        CHECK(!inLockZone(z, 0.5, 0.5));
    }

    // A nonsense side reserves nothing rather than guessing an edge.
    {
        LockZone z;
        z.enabled = true;
        z.side = QStringLiteral("diagonal");
        z.fraction = 0.5;
        CHECK(!inLockZone(z, 0.9, 0.9));
    }

    // A fraction outside the sane range is clamped, so a corrupt config cannot
    // reserve the entire panel and make touch look broken.
    {
        LockZone z;
        z.enabled = true;
        z.side = QStringLiteral("right");
        z.fraction = 5.0;
        CHECK(!inLockZone(z, 0.05, 0.5));   // clamped to 0.9, so the left edge is free
        z.fraction = -1.0;
        CHECK(!inLockZone(z, 0.5, 0.5));    // clamped to 0.05
        CHECK(inLockZone(z, 0.99, 0.5));
    }

    // ---------------------------------------------------------------- config

    // Defaults are off. A config that fails to parse must not start changing
    // the panel when someone brushes the glass.
    {
        const Config c = fromJson(QJsonObject{});
        CHECK(!c.gesturesEnabled);
        CHECK(!c.lockZone.enabled);
        CHECK(c.bindings.isEmpty());
    }

    // An unknown action is dropped on load rather than kept as a binding that
    // silently does nothing.
    {
        const Config c = fromJson(QJsonObject{
            { QStringLiteral("gestures"),
              QJsonObject{ { QStringLiteral("enabled"), true },
                           { QStringLiteral("bindings"),
                             QJsonObject{ { QStringLiteral("swipe-left"), QStringLiteral("profile-next") },
                                          { QStringLiteral("swipe-up"), QStringLiteral("nonsense") } } } } } });
        CHECK(c.gesturesEnabled);
        CHECK(c.bindings.value(QStringLiteral("swipe-left")) == QLatin1String("profile-next"));
        CHECK(!c.bindings.contains(QStringLiteral("swipe-up")));
    }

    // A round trip preserves everything that matters.
    {
        Config c;
        c.gesturesEnabled = true;
        c.bindings.insert(QStringLiteral("two-finger-tap"), QStringLiteral("blank-toggle"));
        c.lockZone = LockZone{ true, QStringLiteral("left"), 0.22 };
        const Config back = fromJson(toJson(c));
        CHECK(back.gesturesEnabled);
        CHECK(back.bindings.value(QStringLiteral("two-finger-tap")) == QLatin1String("blank-toggle"));
        CHECK(back.lockZone.enabled);
        CHECK(back.lockZone.side == QLatin1String("left"));
        CHECK(back.lockZone.fraction > 0.21 && back.lockZone.fraction < 0.23);
    }

    CHECK(isKnownAction(QStringLiteral("profile-next")));
    CHECK(isKnownAction(QStringLiteral("none")));
    CHECK(!isKnownAction(QStringLiteral("launch-nukes")));

    return xen::test::report("test_touchconfig");
}
