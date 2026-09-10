// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/ColorProfiles.h"

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

bool startsWith(const std::string& s, const char* p)
{
    const size_t n = std::char_traits<char>::length(p);
    return s.size() >= n && s.compare(0, n, p) == 0;
}

std::string valueAfter(const std::string& line, const char* key)
{
    const size_t n = std::char_traits<char>::length(key);
    return trim(line.substr(n));
}

std::string basename(const std::string& path)
{
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

bool IccProfile::isAutoEdid() const
{
    // colord names its generated profiles edid-<md5>.icc. Nothing a user
    // calibrates is called that.
    return startsWith(basename(filename), "edid-");
}

const IccProfile* ColorDevice::defaultProfile() const
{
    return profiles.empty() ? nullptr : &profiles.front();
}

bool ColorDevice::hasCalibration() const
{
    return std::any_of(profiles.begin(), profiles.end(),
                       [](const IccProfile& p) { return !p.isAutoEdid(); });
}

std::vector<ColorDevice> parseColorDevices(const std::string& raw)
{
    std::vector<ColorDevice> out;
    std::istringstream in(raw);
    std::string line;
    bool expectProfilePath = false;

    while (std::getline(in, line)) {
        const std::string t = trim(line);

        if (startsWith(t, "Object Path:")) {
            out.emplace_back();
            out.back().objectPath = valueAfter(t, "Object Path:");
            expectProfilePath = false;
            continue;
        }
        if (out.empty())
            continue;   // preamble before the first device
        ColorDevice& d = out.back();

        // A profile id is followed by an indented line holding its path.
        if (expectProfilePath) {
            expectProfilePath = false;
            if (!t.empty() && t.front() == '/' && !d.profiles.empty()) {
                d.profiles.back().filename = t;
                continue;
            }
        }

        if (startsWith(t, "Device ID:")) { d.deviceId = valueAfter(t, "Device ID:"); continue; }
        if (startsWith(t, "Model:"))     { d.model = valueAfter(t, "Model:"); continue; }
        if (startsWith(t, "Vendor:"))    { d.vendor = valueAfter(t, "Vendor:"); continue; }
        if (startsWith(t, "Metadata:")) {
            const std::string meta = valueAfter(t, "Metadata:");
            if (startsWith(meta, "XRANDR_name="))
                d.xrandrName = meta.substr(std::char_traits<char>::length("XRANDR_name="));
            continue;
        }
        if (startsWith(t, "Profile ")) {
            const auto colon = t.find(':');
            if (colon != std::string::npos) {
                IccProfile p;
                p.id = trim(t.substr(colon + 1));
                d.profiles.push_back(std::move(p));
                expectProfilePath = true;
            }
            continue;
        }
    }

    // A block with no device id is not a usable device.
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const ColorDevice& d) { return d.deviceId.empty(); }),
              out.end());
    return out;
}

const ColorDevice* findEdge(const std::vector<ColorDevice>& devices)
{
    // Matched on model. The device id carries vendor strings and trailing
    // padding that differ between machines and even between reboots.
    const auto it = std::find_if(devices.begin(), devices.end(), [](const ColorDevice& d) {
        std::string m = d.model;
        std::transform(m.begin(), m.end(), m.begin(),
                       [](unsigned char c) { return char(std::toupper(c)); });
        return m.find("XENEON EDGE") != std::string::npos;
    });
    return it == devices.end() ? nullptr : &*it;
}

} // namespace xen
