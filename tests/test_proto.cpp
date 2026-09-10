// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/BragiFrame.h"
#include "proto/Commands.h"

#include "check.h"

#include <cstdio>
#include <cstring>

int main()
{
    using xen::BragiFrame;

    // Fresh frame: correct size, report id set, payload zeroed.
    BragiFrame f;
    CHECK(f.size() == 64);
    CHECK(f.data()[0] == 0x01);
    for (size_t i = 0; i < BragiFrame::kPayloadSize; ++i)
        CHECK(f.payload()[i] == 0);

    // Payload set + clamp.
    uint8_t big[100];
    memset(big, 0xAB, sizeof big);
    CHECK(f.setPayload(big, sizeof big) == BragiFrame::kPayloadSize);
    CHECK(f.payload()[0] == 0xAB && f.payload()[62] == 0xAB);

    // Round-trip via raw with report id.
    BragiFrame g = BragiFrame::fromRaw(f.data(), f.size());
    CHECK(memcmp(g.data(), f.data(), 64) == 0);

    // Raw without report id (kernel-stripped read).
    uint8_t stripped[63];
    memset(stripped, 0x5C, sizeof stripped);
    BragiFrame h = BragiFrame::fromRaw(stripped, sizeof stripped);
    CHECK(h.data()[0] == 0x01);
    CHECK(h.payload()[0] == 0x5C && h.payload()[62] == 0x5C);

    // Identity constants pinned to the real hardware.
    static_assert(xen::kVendorId == 0x1B1C && xen::kProductId == 0x1D0D, "device id");

    return xen::test::report("test_proto");
}
