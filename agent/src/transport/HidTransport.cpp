// SPDX-License-Identifier: GPL-3.0-or-later
#include "transport/HidTransport.h"

#include <hidapi/hidapi.h>

namespace xen {

namespace {

std::string narrowError(hid_device* dev)
{
    const wchar_t* w = hid_error(dev);
    if (!w)
        return "unknown hidapi error";
    std::string out;
    for (; *w; ++w)
        out += (*w < 0x80) ? static_cast<char>(*w) : '?';
    return out;
}

} // namespace

HidTransport::~HidTransport()
{
    close();
}

bool HidTransport::open(const std::string& path)
{
    close();
    if (hid_init() != 0) {
        m_error = "hid_init failed";
        return false;
    }
    m_dev = hid_open_path(path.c_str());
    if (!m_dev) {
        m_error = narrowError(nullptr);
        return false;
    }
    return true;
}

void HidTransport::close()
{
    if (m_dev) {
        hid_close(m_dev);
        m_dev = nullptr;
    }
}

int HidTransport::write(const uint8_t* data, size_t len)
{
    if (!m_dev) {
        m_error = "device not open";
        return -1;
    }
    const int n = hid_write(m_dev, data, len);
    if (n < 0)
        m_error = narrowError(m_dev);
    return n;
}

std::vector<uint8_t> HidTransport::read(size_t maxLen, int timeoutMs)
{
    if (!m_dev) {
        m_error = "device not open";
        return {};
    }
    std::vector<uint8_t> buf(maxLen);
    const int n = hid_read_timeout(m_dev, buf.data(), buf.size(), timeoutMs);
    if (n <= 0) {
        if (n < 0)
            m_error = narrowError(m_dev);
        return {};
    }
    buf.resize(static_cast<size_t>(n));
    return buf;
}

} // namespace xen
