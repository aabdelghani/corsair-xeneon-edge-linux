// SPDX-License-Identifier: GPL-3.0-or-later
// Version comparison decides whether users are told to upgrade, and getting it
// wrong is either a nag loop or a silent failure to ship a fix. So it is tested.
#include "core/UpdateChecker.h"

#include <QCoreApplication>

#include "check.h"

#include <cstdio>
#include <string>

using xen::Version;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // Parsing the shapes GitHub tags actually take.
    CHECK(Version::parse(QStringLiteral("v1.2.3")).valid);
    CHECK(Version::parse(QStringLiteral("1.2.3")).valid);
    CHECK(Version::parse(QStringLiteral("V1.2.3")).valid);
    CHECK(Version::parse(QStringLiteral("1.2")).valid);
    const Version v = Version::parse(QStringLiteral("v0.2.0"));
    CHECK(v.major == 0 && v.minor == 2 && v.patch == 0 && v.pre.isEmpty());

    // Two-component tags imply .0
    CHECK(Version::parse(QStringLiteral("v2.1")).patch == 0);

    // Junk must not parse. Guessing at an unknown tag is worse than ignoring it.
    CHECK(!Version::parse(QStringLiteral("")).valid);
    CHECK(!Version::parse(QStringLiteral("nightly")).valid);
    CHECK(!Version::parse(QStringLiteral("v1.2.3.4")).valid);
    CHECK(!Version::parse(QStringLiteral("1.2.3 (beta)")).valid);
    CHECK(!Version::parse(QStringLiteral("release-1.2.3")).valid);

    auto newer = [](const char* a, const char* b) {
        return Version::parse(QString::fromLatin1(a))
            .isNewerThan(Version::parse(QString::fromLatin1(b)));
    };

    // Ordinary ordering.
    CHECK(newer("v0.3.0", "v0.2.0"));
    CHECK(newer("v1.0.0", "v0.9.9"));
    CHECK(newer("v0.2.1", "v0.2.0"));
    CHECK(!newer("v0.2.0", "v0.2.0"));
    CHECK(!newer("v0.2.0", "v0.3.0"));
    CHECK(!newer("v0.2.0", "v1.0.0"));

    // Numeric, not lexicographic. "v0.10.0" beats "v0.9.0".
    CHECK(newer("v0.10.0", "v0.9.0"));
    CHECK(newer("v1.0.10", "v1.0.9"));
    CHECK(!newer("v0.9.0", "v0.10.0"));

    // Semver pre-release ordering: 1.0.0-rc1 is older than 1.0.0.
    CHECK(newer("v1.0.0", "v1.0.0-rc1"));
    CHECK(!newer("v1.0.0-rc1", "v1.0.0"));
    CHECK(newer("v1.0.0-rc2", "v1.0.0-rc1"));
    CHECK(newer("v1.0.0-rc1", "v0.9.0"));

    // An invalid version is never newer, and nothing is ever newer than it.
    // This is what keeps a malformed release feed from nagging every user.
    CHECK(!newer("nightly", "v0.2.0"));
    CHECK(!newer("v0.2.0", "nightly"));

    // The build's own version must be parseable, or the app can never compare.
    CHECK(xen::UpdateChecker::currentVersion().valid);

    // And it must match what the packaging thinks it is building. These are two
    // separate places (a CMake cache variable and ui/package.json) and they
    // silently disagreed once: a 0.4.1 package shipped binaries reporting
    // 0.4.0, because the cache variable kept its first value across a bump.
    // The expected version is passed in by ctest.
    if (argc > 1) {
        const QString expected = QString::fromLocal8Bit(argv[1]);
        const QString actual = xen::UpdateChecker::currentVersion().toString();
        if (actual != expected)
            std::fprintf(stderr, "version mismatch: built %s, packaging says %s\n",
                         actual.toUtf8().constData(), expected.toUtf8().constData());
        CHECK(actual == expected);
    }
    return xen::test::report("test_version");
}
