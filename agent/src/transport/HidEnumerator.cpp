// SPDX-License-Identifier: GPL-3.0-or-later
#include "transport/HidEnumerator.h"

#include "proto/Commands.h"

#include <hidapi/hidapi.h>

#include <codecvt>
#include <locale>

namespace xen {

namespace {

std::string narrow(const wchar_t* ws)
{
    if (!ws)
        return {};
    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
    try {
        return conv.to_bytes(ws);
    } catch (...) {
        return {};
    }
}

} // namespace

std::optional<HidDeviceInfo> HidEnumerator::findEdge(bool checkAccess)
{
    if (hid_init() != 0)
        return std::nullopt;

    std::optional<HidDeviceInfo> found;
    hid_device_info* list = hid_enumerate(kVendorId, kProductId);
    for (hid_device_info* d = list; d; d = d->next) {
        // The Edge exposes a single vendor-page interface; prefer an exact
        // usage-page match but fall back to the first interface seen.
        const bool exact = d->usage_page == kUsagePage;
        if (!found || exact) {
            HidDeviceInfo info;
            info.path      = d->path ? d->path : "";
            info.product   = narrow(d->product_string);
            info.serial    = narrow(d->serial_number);
            info.usagePage = d->usage_page;
            found = info;
            if (exact)
                break;
        }
    }
    hid_free_enumeration(list);

    if (found && checkAccess && !found->path.empty()) {
        if (hid_device* h = hid_open_path(found->path.c_str())) {
            found->accessible = true;
            hid_close(h);
        }
    }
    return found;
}

} // namespace xen
