// edgeline: 64-byte HID report framing for the Xeneon Edge.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Framing facts from the device's own report descriptor (sysfs, 32 bytes):
// vendor usage page 0xFF1B, Report ID 0x01, 63-byte Input + 63-byte Output.
// The payload *semantics* (Bragi/Protocol-V2 hypothesis) are established in
// later milestones; this class only packs/unpacks the container.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace xen {

class BragiFrame {
public:
    static constexpr size_t  kReportSize  = 64; // report id + 63 payload bytes
    static constexpr size_t  kPayloadSize = 63;
    static constexpr uint8_t kReportId    = 0x01;

    BragiFrame(); // zeroed payload, report id set

    // Whole report, as written to / read from the hidraw node.
    [[nodiscard]] const uint8_t* data() const { return m_buf.data(); }
    uint8_t*       data()       { return m_buf.data(); }
    [[nodiscard]] static size_t         size() { return kReportSize; }

    // Payload region (bytes after the report id).
    [[nodiscard]] const uint8_t* payload() const { return m_buf.data() + 1; }
    uint8_t*       payload()       { return m_buf.data() + 1; }

    // Copies up to kPayloadSize bytes; returns bytes actually copied.
    size_t setPayload(const uint8_t* src, size_t len);

    // Parse a raw report as received (with or without leading report id).
    static BragiFrame fromRaw(const uint8_t* src, size_t len);

private:
    std::array<uint8_t, kReportSize> m_buf{};
};

} // namespace xen
