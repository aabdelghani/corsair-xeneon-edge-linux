// SPDX-License-Identifier: GPL-3.0-or-later
// Per-app rules decide when the panel changes behind your back. Getting the
// ordering or the "nothing matched" case wrong means the panel either fights
// you on every alt-tab or never fires at all, and neither failure is loud.
#include "core/AppRules.h"

#include "check.h"

#include <cstdio>

using namespace xen;

namespace {

WindowInfo win(const char* inst, const char* cls, std::vector<std::string> states = {})
{
    WindowInfo w;
    w.wmInstance = inst;
    w.wmClass = cls;
    w.states = std::move(states);
    w.valid = true;
    return w;
}

AppRule rule(const char* pattern, const char* profile, MatchOn on = MatchOn::WmClass)
{
    AppRule r;
    r.pattern = pattern;
    r.profile = profile;
    r.matchOn = on;
    return r;
}

// The four rules the design ships with.
std::vector<AppRule> designRules()
{
    return {
        rule("blender.Blender", "Game"),
        rule("firefox.Navigator", "Desk"),
        rule("darktable.darktable", "sRGB print"),
        rule("_NET_WM_STATE_FULLSCREEN", "Night", MatchOn::WindowState),
    };
}

} // namespace

int main()
{
    // ---------------------------------------------------------- WM_CLASS

    CHECK(wmClassMatches("blender.Blender", win("blender", "Blender")));
    CHECK(!wmClassMatches("blender.Blender", win("firefox", "Navigator")));

    // Capitalisation is not something a user should have to get right by hand.
    CHECK(wmClassMatches("BLENDER.blender", win("blender", "Blender")));
    CHECK(wmClassMatches("blender.blender", win("Blender", "BLENDER")));

    // A bare word matches either half.
    CHECK(wmClassMatches("firefox", win("firefox", "Navigator")));
    CHECK(wmClassMatches("Navigator", win("firefox", "Navigator")));
    CHECK(!wmClassMatches("fire", win("firefox", "Navigator")));   // not a substring match

    // Both halves must match when a dot is given, so a bare-word rule cannot be
    // widened accidentally into a wrong one.
    CHECK(!wmClassMatches("firefox.Blender", win("firefox", "Navigator")));
    CHECK(!wmClassMatches("", win("firefox", "Navigator")));

    // ---------------------------------------------------------- ordering

    {
        // First match wins, as the design's note says.
        std::vector<AppRule> rules = { rule("firefox", "Desk"), rule("firefox", "Night") };
        const auto m = evaluateRules(win("firefox", "Navigator"), rules, "");
        CHECK(m.matched);
        CHECK(m.index == 0);
        CHECK(m.profile == "Desk");
        CHECK(!m.fromFallback);
    }
    {
        // A disabled rule is skipped, and the next one gets its turn.
        std::vector<AppRule> rules = { rule("firefox", "Desk"), rule("firefox", "Night") };
        rules[0].enabled = false;
        const auto m = evaluateRules(win("firefox", "Navigator"), rules, "");
        CHECK(m.matched);
        CHECK(m.index == 1);
        CHECK(m.profile == "Night");
    }
    {
        // A rule naming no profile cannot fire; it would apply nothing.
        std::vector<AppRule> rules = { rule("firefox", ""), rule("firefox", "Night") };
        const auto m = evaluateRules(win("firefox", "Navigator"), rules, "");
        CHECK(m.index == 1);
    }

    // ---------------------------------------------------------- window state

    {
        const auto rules = designRules();
        const auto m = evaluateRules(
            win("mpv", "mpv", { "_NET_WM_STATE_FULLSCREEN" }), rules, "");
        CHECK(m.matched);
        CHECK(m.profile == "Night");
        CHECK(m.index == 3);
    }
    {
        // A WM_CLASS rule earlier in the list beats a later state rule, even
        // when the window is also fullscreen.
        const auto rules = designRules();
        const auto m = evaluateRules(
            win("blender", "Blender", { "_NET_WM_STATE_FULLSCREEN" }), rules, "");
        CHECK(m.profile == "Game");
        CHECK(m.index == 0);
    }
    {
        const auto rules = designRules();
        const auto m = evaluateRules(
            win("mpv", "mpv", { "_NET_WM_STATE_MAXIMIZED_VERT" }), rules, "");
        CHECK(!m.matched);
    }

    // ---------------------------------------------------------- fallback

    {
        // Nothing matched and no fallback: leave the panel alone. Applying
        // something here would fight the user on every alt-tab to a terminal.
        const auto m = evaluateRules(win("xterm", "XTerm"), designRules(), "");
        CHECK(!m.matched);
        CHECK(m.profile.empty());
        CHECK(!m.fromFallback);
    }
    {
        const auto m = evaluateRules(win("xterm", "XTerm"), designRules(), "Desk");
        CHECK(!m.matched);
        CHECK(m.fromFallback);
        CHECK(m.profile == "Desk");
    }
    {
        // Nothing focused at all still honours the fallback, and never claims
        // a rule matched.
        WindowInfo none;
        const auto m = evaluateRules(none, designRules(), "Desk");
        CHECK(!m.matched);
        CHECK(m.fromFallback);
        CHECK(m.profile == "Desk");
        CHECK(m.index == -1);
    }
    {
        const auto m = evaluateRules(WindowInfo{}, {}, "");
        CHECK(!m.matched);
        CHECK(m.profile.empty());
    }

    // ---------------------------------------------------------- names

    CHECK(std::string(matchOnName(MatchOn::WmClass)) == "WM_CLASS");
    CHECK(std::string(matchOnName(MatchOn::WindowState)) == "window state");
    CHECK(matchOnFromName("window state") == MatchOn::WindowState);
    CHECK(matchOnFromName("WINDOW STATE") == MatchOn::WindowState);
    CHECK(matchOnFromName("WM_CLASS") == MatchOn::WmClass);
    CHECK(matchOnFromName("nonsense") == MatchOn::WmClass);   // safe default

    return xen::test::report("test_rules");
}
