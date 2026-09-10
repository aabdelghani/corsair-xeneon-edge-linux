// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/BragiFrame.h"

#include <algorithm>
#include <cstring>

namespace xen {

BragiFrame::BragiFrame()
{
    m_buf.fill(0);
    m_buf[0] = kReportId;
}

size_t BragiFrame::setPayload(const uint8_t* src, size_t len)
{
    const size_t n = std::min(len, kPayloadSize);
    std::memcpy(m_buf.data() + 1, src, n);
    return n;
}

BragiFrame BragiFrame::fromRaw(const uint8_t* src, size_t len)
{
    BragiFrame f;
    if (len == 0)
        return f;
    if (src[0] == kReportId) {
        std::memcpy(f.m_buf.data(), src, std::min(len, kReportSize));
    } else {
        // hidapi read on some kernels strips the report id for single-report devices
        f.setPayload(src, len);
    }
    return f;
}

} // namespace xen
