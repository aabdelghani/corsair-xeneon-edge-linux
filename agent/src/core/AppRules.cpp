// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AppRules.h"

#include <algorithm>
#include <cctype>

namespace xen {
namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

} // namespace

const char* matchOnName(MatchOn m)
{
    return m == MatchOn::WindowState ? "window state" : "WM_CLASS";
}

MatchOn matchOnFromName(const std::string& s)
{
    return lower(s) == "window state" ? MatchOn::WindowState : MatchOn::WmClass;
}

bool wmClassMatches(const std::string& pattern, const WindowInfo& win)
{
    if (pattern.empty())
        return false;
    const std::string p = lower(pattern);
    const std::string inst = lower(win.wmInstance);
    const std::string cls = lower(win.wmClass);

    const auto dot = p.find('.');
    if (dot == std::string::npos) {
        // A bare word matches either half, so "firefox" catches
        // "firefox.Navigator" without the user knowing the second part.
        return !p.empty() && (p == inst || p == cls);
    }
    return p.substr(0, dot) == inst && p.substr(dot + 1) == cls;
}

RuleMatch evaluateRules(const WindowInfo& win,
                        const std::vector<AppRule>& rules,
                        const std::string& fallback)
{
    RuleMatch out;

    if (win.valid) {
        for (size_t i = 0; i < rules.size(); ++i) {
            const AppRule& r = rules[i];
            if (!r.enabled || r.profile.empty())
                continue;

            bool hit = false;
            if (r.matchOn == MatchOn::WmClass) {
                hit = wmClassMatches(r.pattern, win);
            } else {
                const std::string want = lower(r.pattern);
                hit = std::any_of(win.states.begin(), win.states.end(),
                                  [&](const std::string& s) { return lower(s) == want; });
            }
            if (hit) {
                out.matched = true;
                out.index = int(i);
                out.profile = r.profile;
                return out;
            }
        }
    }

    // Nothing matched. An empty fallback means "leave the panel alone", which
    // is different from "apply nothing": resetting on every unmatched window
    // would fight the user every time they alt-tabbed to a terminal.
    if (!fallback.empty()) {
        out.profile = fallback;
        out.fromFallback = true;
    }
    return out;
}

} // namespace xen
