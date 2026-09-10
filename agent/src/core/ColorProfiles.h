// edgeline: the panel's ICC profile, through colord.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// colord already knows the Edge as a display device and assigns it an
// automatic profile derived from its EDID. That auto profile is not a
// calibration and must not be mistaken for one: telling someone their panel is
// colour managed because an EDID profile exists would be false.
//
// Parsing is pure and lives here so it can be tested against captured
// colormgr output; running colormgr is a separate, thin layer.
#pragma once

#include <string>
#include <vector>

namespace xen {

struct IccProfile {
    std::string id;        // "icc-7423f5ca..."
    std::string filename;  // absolute path, when colormgr reported one
    // True for colord's automatic EDID-derived profile. Present on every
    // display, means nothing was measured.
    [[nodiscard]] bool isAutoEdid() const;
};

struct ColorDevice {
    std::string objectPath;
    std::string deviceId;
    std::string model;
    std::string vendor;
    std::string xrandrName;   // from the OutputEdidMd5/XRANDR_name metadata
    std::vector<IccProfile> profiles;

    // The profile colord would use, which is the first one listed.
    [[nodiscard]] const IccProfile* defaultProfile() const;
    // A real calibration is bound, as opposed to the automatic EDID profile.
    [[nodiscard]] bool hasCalibration() const;
};

// Parse `colormgr get-devices-by-kind display`.
std::vector<ColorDevice> parseColorDevices(const std::string& raw);

// Pick the Edge out of a device list by model, since the device id contains
// padding and vendor strings that differ between machines.
const ColorDevice* findEdge(const std::vector<ColorDevice>& devices);

} // namespace xen
