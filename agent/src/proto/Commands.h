// edgeline: protocol constants for the Corsair Xeneon Edge.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Device identity is verified against the real hardware on this machine.
// Command opcodes are added only once they are proven against the device; see
// PROTOCOL.md for sources and status.
#pragma once

#include "proto/BragiFrame.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace xen {

constexpr uint16_t kVendorId  = 0x1B1C; // CORSAIR
constexpr uint16_t kProductId = 0x1D0D; // XENEON EDGE
constexpr uint16_t kUsagePage = 0xFF1B; // vendor page, from the device's report descriptor
constexpr uint16_t kUsage     = 0x0091;

// Bragi / Protocol-V2 command bytes (CANDIDATE — see PROTOCOL.md).
namespace cmd {
constexpr uint8_t kSet = 0x01;
constexpr uint8_t kGet = 0x02;
} // namespace cmd

namespace prop {
constexpr uint8_t kFirmware = 0x13; // GetFirmware property id
} // namespace prop

// Builds the firmware-version query as a 64-byte report.
// Layout: [0x01 report id][0x02 GET][0x13 firmware][zeros...].
// This is a *read* request (GET); it does not change device state.
inline BragiFrame buildFirmwareQuery()
{
    BragiFrame f;
    uint8_t payload[] = { cmd::kGet, prop::kFirmware };
    f.setPayload(payload, sizeof payload);
    return f;
}

// Picture parameters held by the panel controller, read and written over HID.
// VERIFIED against the device (fw 20250714V1). Frame:
// [0x01][cmd][00 00 00 00][payload...], and a reply carries its data from
// report offset 6. Brightness exists only here: it has no DDC/CI code, while
// backlight and contrast mirror VCP 0x10 and 0x12. See PROTOCOL.md section 6.
namespace screen {
constexpr uint8_t kGetParams = 0x0E;
constexpr uint8_t kSetParam  = 0x0F;
constexpr size_t  kCmdOffset  = 1; // within the whole report
constexpr size_t  kArgOffset  = 6;
constexpr size_t  kDataOffset = 6;

// SetParam selectors: {group, item}.
constexpr uint8_t kBacklight[]  = { 0x02, 0x00 };
constexpr uint8_t kContrast[]   = { 0x02, 0x01 };
constexpr uint8_t kBrightness[] = { 0x02, 0x02 };

struct Params {
    int brightness = 0;
    int backlight = 0;
    int contrast = 0;
    int red = 0;
    int green = 0;
    int blue = 0;
};
} // namespace screen

inline BragiFrame buildScreenParamsQuery()
{
    BragiFrame f;
    f.data()[screen::kCmdOffset] = screen::kGetParams;
    return f;
}

inline BragiFrame buildSetScreenParam(const uint8_t (&selector)[2], uint8_t value)
{
    BragiFrame f;
    f.data()[screen::kCmdOffset]     = screen::kSetParam;
    f.data()[screen::kArgOffset]     = selector[0];
    f.data()[screen::kArgOffset + 1]     = selector[1];
    f.data()[screen::kArgOffset + 2]     = value;
    return f;
}

// Accepts only a GetParams reply (report id 0x01, cmd echo 0x0E), so a stale
// report left in the queue is never mistaken for the panel's settings.
inline std::optional<screen::Params> parseScreenParams(const std::vector<uint8_t>& rx)
{
    if (rx.size() < screen::kDataOffset + 6 || rx[0] != BragiFrame::kReportId
        || rx[screen::kCmdOffset] != screen::kGetParams)
        return std::nullopt;
    const uint8_t* d = rx.data() + screen::kDataOffset;
    return screen::Params{ d[0], d[1], d[2], d[3], d[4], d[5] };
}

} // namespace xen
