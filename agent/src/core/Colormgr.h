// edgeline: running colormgr, the thin layer over the ColorProfiles parser.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Kept out of the agent so the process side can be tested against a stand-in
// colormgr, the same way the parser is tested against captured output.
#pragma once

#include "core/ColorProfiles.h"

#include <vector>

namespace xen {

// Run `colormgr get-devices-by-kind display` and parse it. Empty when colord
// is not running, colormgr is not installed, or it fails.
std::vector<ColorDevice> readColorDevices();

} // namespace xen
