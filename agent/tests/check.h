// SPDX-License-Identifier: GPL-3.0-or-later
// A check that survives NDEBUG.
//
// These suites used plain assert(), which the Release build compiles out via
// NDEBUG. That made `ctest` pass instantly while testing nothing, which is
// worse than having no tests at all: it reports confidence it has not earned.
#pragma once

#include <cstdio>
#include <cstdlib>

namespace xen::test {

inline int failures = 0;

inline void check(bool ok, const char* expr, const char* file, int line)
{
    if (ok)
        return;
    ++failures;
    std::fprintf(stderr, "%s:%d: FAILED: %s\n", file, line, expr);
}

inline int report(const char* suite)
{
    if (failures != 0) {
        std::fprintf(stderr, "%s: %d assertion(s) failed\n", suite, failures);
        return 1;
    }
    std::printf("%s: all assertions passed\n", suite);
    return 0;
}

} // namespace xen::test

#define CHECK(expr) ::xen::test::check((expr), #expr, __FILE__, __LINE__)
