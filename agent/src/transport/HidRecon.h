// edgeline: read-only HID reconnaissance (M3). NO WRITES happen here.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "transport/HidEnumerator.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xen {

struct ReconReport {
    HidDeviceInfo info;
    bool haveInfo = false;

    std::vector<uint8_t> reportDescriptor; // raw bytes from sysfs
    std::string descriptorDecode;          // human-readable decode
    std::string descriptorSource;          // sysfs path used, or why unavailable

    // Passive read: did the device volunteer any input report on its own?
    bool passiveReadAttempted = false;
    std::vector<uint8_t> passiveReport; // empty if nothing arrived
    std::string passiveNote;
};

class HidRecon {
public:
    // Purely read-only. Enumerates, reads the sysfs report descriptor, and
    // does a single short passive read (no report is ever written).
    static ReconReport run(int passiveTimeoutMs = 300);
};

} // namespace xen
