// edgeline: raw HID read/write to the Edge's vendor interface.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Transport only: no command knowledge lives here. All writes in the app
// must go through core/WriteGate (M4+); this class is the mechanism.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct hid_device_; // hidapi forward decl
using hid_device = hid_device_;

namespace xen {

class HidTransport {
public:
    HidTransport() = default;
    ~HidTransport();
    HidTransport(const HidTransport&) = delete;
    HidTransport& operator=(const HidTransport&) = delete;

    bool open(const std::string& path);
    void close();
    [[nodiscard]] bool isOpen() const { return m_dev != nullptr; }

    // Writes a full report (report id included). Returns bytes written or -1.
    int write(const uint8_t* data, size_t len);

    // Reads one report with timeout; empty vector on timeout/error.
    std::vector<uint8_t> read(size_t maxLen, int timeoutMs);

    [[nodiscard]] std::string lastError() const { return m_error; }

private:
    hid_device* m_dev = nullptr;
    std::string m_error;
};

} // namespace xen
