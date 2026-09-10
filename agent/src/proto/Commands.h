// edgeline: protocol constants for the Corsair Xeneon Edge.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Device identity is verified against the real hardware on this machine.
// Command opcodes are intentionally ABSENT until the M3/M4 recon milestones
// prove them against the device; see PROTOCOL.md for sources and status.
#pragma once

#include "proto/BragiFrame.h"

#include <cstdint>

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

} // namespace xen
