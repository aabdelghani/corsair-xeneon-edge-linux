// edgeline: match the focused window against per-app rules.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Deliberately pure. No X11, no profile writing, no settings: it takes a
// description of the focused window and a list of rules, and says which
// profile should be active. That makes the part which is easy to get subtly
// wrong (ordering, matching, what happens when nothing matches) testable
// without a display or a panel attached.
#pragma once

#include <string>
#include <vector>

namespace xen {

// What a rule looks at. The design names exactly these two.
enum class MatchOn {
    WmClass,      // the window's WM_CLASS, as "instance.class"
    WindowState,  // an EWMH state atom such as _NET_WM_STATE_FULLSCREEN
};

struct AppRule {
    std::string app;      // display name, e.g. "Blender"
    std::string pattern;  // "blender.Blender", or "_NET_WM_STATE_FULLSCREEN"
    MatchOn matchOn = MatchOn::WmClass;
    std::string profile;  // profile to apply
    bool enabled = true;
};

struct WindowInfo {
    std::string wmInstance;            // WM_CLASS[0], e.g. "blender"
    std::string wmClass;               // WM_CLASS[1], e.g. "Blender"
    std::string title;
    std::vector<std::string> states;   // _NET_WM_STATE atom names
    bool valid = false;                // false when nothing is focused
};

struct RuleMatch {
    bool matched = false;    // a rule fired
    int index = -1;          // which one, for showing in the UI
    std::string profile;     // profile to apply (may be the fallback)
    bool fromFallback = false;
};

// First match wins, which is what the design's "first match wins" note means.
// Disabled rules are skipped. When nothing matches, `fallback` is returned if
// non-empty, otherwise the result is "no opinion" and the caller should leave
// the panel alone rather than resetting it.
RuleMatch evaluateRules(const WindowInfo& win,
                        const std::vector<AppRule>& rules,
                        const std::string& fallback);

// "blender.Blender" against instance "blender" class "Blender".
// Matching is case-insensitive, because WM_CLASS capitalisation is not
// something a user should have to get exactly right by hand. A pattern with no
// dot matches either half, so "firefox" catches "firefox.Navigator".
bool wmClassMatches(const std::string& pattern, const WindowInfo& win);

const char* matchOnName(MatchOn m);
MatchOn matchOnFromName(const std::string& s);

} // namespace xen
