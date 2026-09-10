// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DdcCapabilities.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace xen {
namespace {

std::string trim(const std::string& s)
{
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
        return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Leading run of hex digits, e.g. "0B" from "0B: User 1". Returns false when
// the token is not hex, which is how prose lines are rejected.
bool parseHexByte(const std::string& tok, uint8_t& out)
{
    if (tok.empty() || tok.size() > 2)
        return false;
    int v = 0;
    for (char ch : tok) {
        if (!std::isxdigit(static_cast<unsigned char>(ch)))
            return false;
        v = v * 16 + (std::isdigit(static_cast<unsigned char>(ch))
                          ? ch - '0'
                          : (std::tolower(static_cast<unsigned char>(ch)) - 'a' + 10));
    }
    out = static_cast<uint8_t>(v);
    return true;
}

std::string afterColon(const std::string& line)
{
    const auto c = line.find(':');
    return c == std::string::npos ? std::string() : trim(line.substr(c + 1));
}

} // namespace

const VcpFeature* DdcCapabilities::find(uint8_t code) const
{
    const auto it = std::find_if(features.begin(), features.end(),
                                 [code](const VcpFeature& f) { return f.code == code; });
    return it == features.end() ? nullptr : &*it;
}

bool DdcCapabilities::has(uint8_t code) const { return find(code) != nullptr; }

DdcCapabilities parseCapabilities(const std::string& raw)
{
    DdcCapabilities caps;
    std::istringstream in(raw);
    std::string line;

    bool inValues = false;   // inside a "Values:" block of the current feature

    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty())
            continue;

        if (t.rfind("Model:", 0) == 0) {
            caps.model = afterColon(t);
            continue;
        }
        if (t.rfind("MCCS version:", 0) == 0) {
            caps.mccs = afterColon(t);
            continue;
        }
        if (t == "Values:") {
            inValues = true;
            continue;
        }
        if (t.rfind("Feature:", 0) == 0) {
            // "Feature: 14 (Select color preset)"
            inValues = false;
            const std::string rest = afterColon(t);
            const auto sp = rest.find(' ');
            const std::string codeTok = sp == std::string::npos ? rest : rest.substr(0, sp);
            uint8_t code = 0;
            if (!parseHexByte(codeTok, code))
                continue;
            VcpFeature f;
            f.code = code;
            const auto lp = rest.find('(');
            const auto rp = rest.rfind(')');
            if (lp != std::string::npos && rp != std::string::npos && rp > lp)
                f.name = trim(rest.substr(lp + 1, rp - lp - 1));
            caps.features.push_back(std::move(f));
            continue;
        }
        if (inValues && !caps.features.empty()) {
            // "01: sRGB"
            const auto c = t.find(':');
            if (c == std::string::npos)
                continue;
            uint8_t v = 0;
            if (!parseHexByte(trim(t.substr(0, c)), v))
                continue;
            caps.features.back().values.push_back(VcpValue{ v, trim(t.substr(c + 1)) });
            continue;
        }
        // Anything else (Commands:, Op Code: lines, prose) is deliberately
        // ignored rather than guessed at.
        if (t.rfind("Op Code:", 0) == 0 || t == "Commands:" || t == "VCP Features:")
            inValues = false;
    }
    return caps;
}

} // namespace xen
