// SPDX-License-Identifier: GPL-3.0-or-later
// Parsed against the real capability string this panel emits, captured to
// tests/fixtures/. The design mocked up four presets and two input sources;
// the hardware reports seven and seven. This test exists so the UI keeps being
// driven by the panel rather than by the mockup.
#include "core/DdcCapabilities.h"

#include "check.h"

#include <fstream>
#include <sstream>
#include <string>

using xen::parseCapabilities;

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
    const char* fixture = argc > 1 ? argv[1] : "tests/fixtures/xeneon-edge-capabilities.txt";
    const std::string raw = slurp(fixture);
    CHECK(!raw.empty());

    const auto caps = parseCapabilities(raw);

    CHECK(caps.model == "RTK");
    CHECK(caps.mccs == "2.2");

    // Every control the Picture page draws must be backed by a real feature.
    CHECK(caps.has(0x10));  // brightness
    CHECK(caps.has(0x12));  // contrast
    CHECK(caps.has(0x14));  // colour preset
    CHECK(caps.has(0x16));  // red gain
    CHECK(caps.has(0x18));  // green gain
    CHECK(caps.has(0x1A));  // blue gain
    CHECK(caps.has(0x87));  // sharpness
    CHECK(caps.has(0x60));  // input source
    CHECK(caps.has(0xD6));  // power mode
    CHECK(caps.has(0x04));  // restore factory defaults
    CHECK(caps.has(0x05));  // restore brightness/contrast defaults
    CHECK(caps.has(0x08));  // restore colour defaults

    // Something absent must read as absent, not as a silent default.
    CHECK(!caps.has(0x99));
    CHECK(caps.find(0x99) == nullptr);

    // Continuous features carry no value list.
    const auto* bright = caps.find(0x10);
    CHECK(bright != nullptr);
    CHECK(bright->continuous());
    CHECK(bright->name == "Brightness");

    // The preset list is the whole point: seven entries, not the mockup's four.
    const auto* preset = caps.find(0x14);
    CHECK(preset != nullptr);
    CHECK(!preset->continuous());
    CHECK(preset->values.size() == 7);
    CHECK(preset->values[0].code == 0x01);
    CHECK(preset->values[0].label == "sRGB");
    bool sawUser1 = false;
    for (const auto& v : preset->values)
        if (v.code == 0x0B && v.label == "User 1")
            sawUser1 = true;
    CHECK(sawUser1);   // RGB gain unlocks on this one

    // Input source: real values, none of which is the USB-C the panel has.
    const auto* input = caps.find(0x60);
    CHECK(input != nullptr);
    CHECK(!input->continuous());
    CHECK(input->values.size() == 7);
    bool sawUsbC = false;
    for (const auto& v : input->values)
        if (v.label.find("USB") != std::string::npos)
            sawUsbC = true;
    CHECK(!sawUsbC);

    // Junk must not produce half-parsed features.
    {
        const auto empty = parseCapabilities("");
        CHECK(empty.features.empty());
        CHECK(!empty.has(0x10));
    }
    {
        const auto prose = parseCapabilities("this is not a capability string\nat all\n");
        CHECK(prose.features.empty());
    }
    {
        // A malformed feature code must be skipped, not coerced to zero.
        const auto bad = parseCapabilities("VCP Features:\n   Feature: ZZ (Nonsense)\n");
        CHECK(bad.features.empty());
    }

    return xen::test::report("test_capabilities");
}
