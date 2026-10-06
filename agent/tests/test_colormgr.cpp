// SPDX-License-Identifier: GPL-3.0-or-later
// colormgr translates its labels. On a German desktop it prints "Objektpfad:"
// and "Modell:" where the parser looks for "Object Path:" and "Model:", so no
// device parses, the Edge drops off the colour page, and the page blames
// colord for not running. The parser is right to expect one fixed language;
// the fix belongs where colormgr is started. So this runs readColorDevices
// against a stand-in colormgr that answers in German unless the message
// locale is pinned, deciding it the way gettext does.
#include "core/Colormgr.h"

#include "check.h"

#include <QCoreApplication>

#include <cstdlib>
#include <string>

using namespace xen;

namespace {

// The locale a desktop session hands the agent, replacing whatever the test
// runner itself carries.
void sessionLocale(const char* lang, const char* lcMessages, const char* language)
{
    unsetenv("LC_ALL");
    lang ? setenv("LANG", lang, 1) : unsetenv("LANG");
    lcMessages ? setenv("LC_MESSAGES", lcMessages, 1) : unsetenv("LC_MESSAGES");
    language ? setenv("LANGUAGE", language, 1) : unsetenv("LANGUAGE");
}

bool findsEdge()
{
    const auto devices = readColorDevices();
    const ColorDevice* edge = findEdge(devices);
    return edge != nullptr && edge->model == "XENEON EDGE";
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const char* fakeBin = argc > 1 ? argv[1] : "tests/fixtures/fake-colormgr";
    const char* oldPath = std::getenv("PATH");
    const std::string path = std::string(fakeBin) + ":" + (oldPath ? oldPath : "");
    setenv("PATH", path.c_str(), 1);

    // An English session, the case that has always worked.
    sessionLocale("C.UTF-8", nullptr, nullptr);
    CHECK(findsEdge());

    // A German session with only LANG set, which is all the agent inherits
    // when it is started from a desktop session.
    sessionLocale("de_DE.UTF-8", nullptr, nullptr);
    CHECK(findsEdge());

    // LC_MESSAGES alone translates, so pinning LANG would not be enough.
    sessionLocale("C.UTF-8", "de_DE.UTF-8", nullptr);
    CHECK(findsEdge());

    // LANGUAGE picks the translation on an otherwise English locale.
    sessionLocale("en_US.UTF-8", nullptr, "de");
    CHECK(findsEdge());

    return xen::test::report("test_colormgr");
}
