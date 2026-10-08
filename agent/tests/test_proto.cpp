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

    // Screen parameters: bytes as captured from the panel (fw 20250714V1).
    const BragiFrame q = xen::buildScreenParamsQuery();
    CHECK(q.data()[0] == 0x01 && q.data()[1] == 0x0E);
    for (size_t i = 2; i < BragiFrame::kReportSize; ++i)
        CHECK(q.data()[i] == 0);

    const BragiFrame s = xen::buildSetScreenParam(xen::screen::kBrightness, 40);
    const uint8_t wantSet[] = { 0x01, 0x0F, 0, 0, 0, 0, 0x02, 0x02, 0x28, 0 };
    CHECK(memcmp(s.data(), wantSet, sizeof wantSet) == 0);
    CHECK(xen::buildSetScreenParam(xen::screen::kBacklight, 95).data()[7] == 0x00);
    CHECK(xen::buildSetScreenParam(xen::screen::kContrast, 51).data()[7] == 0x01);

    std::vector<uint8_t> reply(64, 0);
    const uint8_t got[] = { 0x01, 0x0E, 0, 0, 0, 0x1D, 0x32, 0x5F, 0x33, 0x68, 0x69, 0x6B };
    memcpy(reply.data(), got, sizeof got);
    const auto p = xen::parseScreenParams(reply);
    CHECK(p.has_value());
    CHECK(p->brightness == 50 && p->backlight == 95 && p->contrast == 51);
    CHECK(p->red == 104 && p->green == 105 && p->blue == 107);

    // A set acknowledgement or a short read is not a parameter reply.
    std::vector<uint8_t> ack(64, 0);
    const uint8_t ackBytes[] = { 0x01, 0x0F, 0x0F, 0, 0, 0x02, 0x02, 0x01, 0x28 };
    memcpy(ack.data(), ackBytes, sizeof ackBytes);
    CHECK(!xen::parseScreenParams(ack).has_value());
    CHECK(!xen::parseScreenParams(std::vector<uint8_t>(got, got + 8)).has_value());

    return xen::test::report("test_proto");
}
