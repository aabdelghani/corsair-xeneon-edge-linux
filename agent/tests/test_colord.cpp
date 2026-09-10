// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
// Parsed against real colormgr output captured from this machine. The point of
// this test is the distinction the design's "gain locked while a profile is
// bound" note depends on: colord gives every display an automatic EDID-derived
// profile, and treating that as a calibration would lock the gain sliders on a
// panel nobody ever measured.
#include "core/ColorProfiles.h"

#include "check.h"

#include <fstream>
#include <sstream>

using namespace xen;

namespace {
std::string slurp(const char* path)
{
    std::ifstream in(path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
} // namespace

int main(int argc, char** argv)
{
    const char* fixture = argc > 1 ? argv[1] : "tests/fixtures/colormgr-display-devices.txt";
    const std::string raw = slurp(fixture);
    CHECK(!raw.empty());

    const auto devices = parseColorDevices(raw);
    CHECK(devices.size() == 2);   // the main monitor and the Edge

    // The Edge is found by model, not by device id: the id carries vendor
    // strings and trailing padding that differ between machines.
    const ColorDevice* edge = findEdge(devices);
    CHECK(edge != nullptr);
    CHECK(edge->model == "XENEON EDGE");
    CHECK(edge->xrandrName == "DisplayPort-1-4");
    CHECK(!edge->deviceId.empty());
    CHECK(!edge->objectPath.empty());

    // It has exactly one profile, and that profile is colord's automatic one.
    CHECK(edge->profiles.size() == 1);
    const IccProfile* def = edge->defaultProfile();
    CHECK(def != nullptr);
    CHECK(def->id == "icc-7423f5ca4e818246893cfc537577201d");
    CHECK(def->filename.find("/edid-") != std::string::npos);
    CHECK(def->isAutoEdid());

    // So the panel is NOT calibrated, and gain must stay editable.
    CHECK(!edge->hasCalibration());

    // The other display parses too, and is not mistaken for the Edge.
    const auto aoc = std::find_if(devices.begin(), devices.end(),
                                  [](const ColorDevice& d) { return d.model == "CU34G2XP"; });
    CHECK(aoc != devices.end());
    CHECK(aoc->vendor == "AOC");
    CHECK(aoc->profiles.size() == 1);
    CHECK(aoc->profiles[0].isAutoEdid());

    // A real calibration is recognised as one.
    {
        ColorDevice d;
        d.deviceId = "x";
        d.profiles.push_back(IccProfile{ "icc-aaa", "/home/q/.local/share/icc/edge-d65.icc" });
        CHECK(!d.profiles[0].isAutoEdid());
        CHECK(d.hasCalibration());
    }
    {
        // A calibration listed after the auto profile still counts.
        ColorDevice d;
        d.deviceId = "x";
        d.profiles.push_back(IccProfile{ "icc-aaa", "/home/q/.local/share/icc/edid-1234.icc" });
        d.profiles.push_back(IccProfile{ "icc-bbb", "/home/q/.local/share/icc/measured.icc" });
        CHECK(d.hasCalibration());
        CHECK(d.defaultProfile()->isAutoEdid());   // but the default is still the auto one
    }
    {
        // A profile with no filename cannot be assumed to be a calibration.
        ColorDevice d;
        d.deviceId = "x";
        d.profiles.push_back(IccProfile{ "icc-ccc", "" });
        CHECK(!d.profiles[0].isAutoEdid());
    }

    // Junk must not produce phantom devices.
    CHECK(parseColorDevices("").empty());
    CHECK(parseColorDevices("no devices found\n").empty());
    // A block with no Device ID is not a usable device.
    CHECK(parseColorDevices("Object Path:   /org/x\nModel:  Nothing\n").empty());

    return xen::test::report("test_colord");
}
