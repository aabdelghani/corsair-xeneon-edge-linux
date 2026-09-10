// edgeline: touch-stack diagnostic for the Edge's digitizer.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Edge ships two USB devices: the Bragi control channel (1b1c:1d0d, see
// PROTOCOL.md) and a separate touch controller (27c0:0859, "wch.cn
// TouchScreen"). This probe answers one question the Bragi channel cannot:
// how many simultaneous contacts does the panel actually deliver on Linux?
//
// It reports both halves of the answer:
//   - what the kernel decided (which HID driver bound each interface), and
//   - what the X server advertises (XITouchClass num_touches), and
//   - optionally, what the panel really delivers under your fingers.
//
// Read-only. Sends nothing to either USB device.
#pragma once

#include <string>
#include <vector>

namespace xen {

struct TouchInterface {
    std::string hidId;      // e.g. "0003:27C0:0859.0012"
    std::string driver;     // "hid-multitouch", "hid-generic", ...
    std::string inputName;  // evdev device name
    std::string eventNode;  // "/dev/input/event22"
    size_t descriptorBytes = 0;
    int fingerCollections = 0;   // Digitizer/Finger collections in the descriptor
    int contactCountMax = 0;     // from the Contact Count Maximum feature, if found
    int configReportId = -1;     // Device Configuration feature report id (0x21 typically)
};

struct TouchXDevice {
    int id = -1;
    std::string name;
    std::string eventNode;
    bool hasTouchClass = false;
    int maxContacts = 0;
    bool directMode = false;
};

struct TouchReport {
    bool haveDigitizer = false;
    std::string usbId;                       // "27c0:0859"
    std::vector<TouchInterface> interfaces;  // kernel side
    std::vector<TouchXDevice> xdevices;      // X server side
    std::string verdict;                     // human-readable summary
    std::string note;                        // caveats / how to go deeper
};

class TouchProbe {
public:
    // Read-only inspection of the kernel + X view of the touch stack.
    static TouchReport run();

    // Live capture: listen for raw XI2 touch for `seconds` and record the peak
    // number of simultaneous contacts. Returns peak, or -1 on failure.
    // `beginCount` receives the total number of touch-begin events seen.
    static int capturePeak(int seconds, long* beginCount, std::string* error);
};

} // namespace xen
