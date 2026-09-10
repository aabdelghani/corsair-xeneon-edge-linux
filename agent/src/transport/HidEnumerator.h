// edgeline: locate the Xeneon Edge's vendor HID interface.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>

namespace xen {

struct HidDeviceInfo {
    std::string path;      // hidraw path, e.g. /dev/hidraw4
    std::string product;   // USB product string
    std::string serial;    // USB serial string
    unsigned short usagePage = 0;
    bool accessible = false; // hid_open_path succeeded for the current user
};

class HidEnumerator {
public:
    // Enumerate 1b1c:1d0d and return its vendor-page interface, if present.
    // checkAccess additionally attempts a (side-effect-free) open to see
    // whether the hidraw node is readable by the current user.
    static std::optional<HidDeviceInfo> findEdge(bool checkAccess = true);
};

} // namespace xen
