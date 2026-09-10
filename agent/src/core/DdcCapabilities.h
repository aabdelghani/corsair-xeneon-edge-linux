// edgeline: parse a ddcutil capability string.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The design mocks up a fixed set of colour presets and a fixed pair of input
// sources. Real panels disagree, and this one disagrees twice: it offers seven
// presets rather than four, and it advertises seven input sources none of which
// is the USB-C the panel physically has. So the UI is driven from what the
// panel reports here, not from what the mockup drew.
//
// Pure string parsing, no Qt process handling, so it can be tested against
// captured output.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xen {

struct VcpValue {
    uint8_t code = 0;      // e.g. 0x05
    std::string label;     // e.g. "6500 K"
};

struct VcpFeature {
    uint8_t code = 0;              // e.g. 0x14
    std::string name;              // e.g. "Select color preset"
    std::vector<VcpValue> values;  // empty for continuous features
    [[nodiscard]] bool continuous() const { return values.empty(); }
};

struct DdcCapabilities {
    std::string model;
    std::string mccs;
    std::vector<VcpFeature> features;

    [[nodiscard]] bool has(uint8_t code) const;
    [[nodiscard]] const VcpFeature* find(uint8_t code) const;
};

// Never throws and never half-fails: unparseable input yields empty features,
// which the UI reads as "this control is unsupported" rather than guessing.
DdcCapabilities parseCapabilities(const std::string& raw);

} // namespace xen
