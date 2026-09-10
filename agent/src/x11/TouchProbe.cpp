// edgeline: touch-stack diagnostic for the Edge's digitizer.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "x11/TouchProbe.h"

#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>

#include <sys/select.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace fs = std::filesystem;

namespace xen {
namespace {

constexpr const char* kHidBusPath = "/sys/bus/hid/devices";
constexpr const char* kTouchHidMatch = "27C0:0859";

std::string readFirstLine(const fs::path& p)
{
    std::ifstream in(p);
    std::string s;
    std::getline(in, s);
    return s;
}

std::string linkName(const fs::path& p)
{
    std::error_code ec;
    const fs::path target = fs::read_symlink(p, ec);
    return ec ? std::string() : target.filename().string();
}

std::vector<uint8_t> readDescriptor(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}

// Walk a HID report descriptor counting the things that decide multi-touch:
// Digitizer/Finger collections, the Contact Count Maximum feature value, and
// the Device Configuration report id (the one hid-multitouch writes to switch
// the panel out of single-contact mouse emulation).
void parseDescriptor(const std::vector<uint8_t>& d, TouchInterface& out)
{
    out.descriptorBytes = d.size();
    unsigned page = 0;
    unsigned usage = 0;
    int reportId = -1;
    int logicalMax = 0;
    bool inDeviceConfig = false;
    int configDepth = 0;

    for (size_t i = 0; i < d.size();) {
        const uint8_t b = d[i++];
        if (b == 0xFE) { // long item
            if (i >= d.size()) break;
            const uint8_t sz = d[i];
            i += static_cast<size_t>(2) + sz;
            continue;
        }
        size_t size = b & 0x03u;
        if (size == 3) size = 4;
        const unsigned type = (b >> 2) & 0x03u;
        const unsigned tag = (b >> 4) & 0x0Fu;
        unsigned val = 0;
        for (size_t k = 0; k < size && i + k < d.size(); ++k)
            val |= static_cast<unsigned>(d[i + k]) << (8 * k);
        i += size;

        if (type == 1 && tag == 0) page = val;               // Usage Page
        if (type == 1 && tag == 2) logicalMax = static_cast<int>(val); // Logical Max
        if (type == 1 && tag == 8) {                          // Report ID
            reportId = static_cast<int>(val);
            if (inDeviceConfig && out.configReportId < 0)
                out.configReportId = reportId;
        }
        if (type == 2 && tag == 0) {                          // Usage
            usage = val;
            if (page == 0x0D && val == 0x0E) {               // Device Configuration
                inDeviceConfig = true;
                configDepth = 0;
                out.configReportId = -1;
            }
            // Contact Count Maximum is a feature whose logical max carries the count.
            if (page == 0x0D && (val == 0x55 || val == 0x53))
                out.contactCountMax = logicalMax > out.contactCountMax ? logicalMax
                                                                       : out.contactCountMax;
        }
        if (type == 0 && tag == 10) { // Collection
            if (page == 0x0D && usage == 0x22 && !inDeviceConfig)
                out.fingerCollections++;
            if (inDeviceConfig) configDepth++;
        }
        if (type == 0 && tag == 12 && inDeviceConfig) { // End Collection
            if (--configDepth <= 0) inDeviceConfig = false;
        }
        // Contact Count Maximum sits in a feature item; capture its logical max.
        if (type == 0 && tag == 11 && page == 0x0D && usage == 0x55 && logicalMax > 0)
            out.contactCountMax = logicalMax;
    }
}

void collectKernelSide(TouchReport& rep)
{
    std::error_code ec;
    if (!fs::exists(kHidBusPath, ec)) return;

    for (const auto& e : fs::directory_iterator(kHidBusPath, ec)) {
        const std::string id = e.path().filename().string();
        if (id.find(kTouchHidMatch) == std::string::npos) continue;

        rep.haveDigitizer = true;
        rep.usbId = "27c0:0859";

        TouchInterface ti;
        ti.hidId = id;
        ti.driver = linkName(e.path() / "driver");
        if (ti.driver.empty()) ti.driver = "none";

        const fs::path inputDir = e.path() / "input";
        if (fs::exists(inputDir, ec)) {
            for (const auto& in : fs::directory_iterator(inputDir, ec)) {
                ti.inputName = readFirstLine(in.path() / "name");
                for (const auto& sub : fs::directory_iterator(in.path(), ec)) {
                    const std::string n = sub.path().filename().string();
                    if (n.rfind("event", 0) == 0) ti.eventNode = "/dev/input/" + n;
                }
            }
        }
        parseDescriptor(readDescriptor(e.path() / "report_descriptor"), ti);
        rep.interfaces.push_back(std::move(ti));
    }
    std::sort(rep.interfaces.begin(), rep.interfaces.end(),
              [](const TouchInterface& a, const TouchInterface& b) { return a.hidId < b.hidId; });
}

std::string xEventNode(Display* dpy, int deviceid)
{
    static Atom nodeAtom = None;
    if (nodeAtom == None) nodeAtom = XInternAtom(dpy, "Device Node", True);
    if (nodeAtom == None) return {};

    Atom type = None;
    int fmt = 0;
    unsigned long nitems = 0, bytes = 0;
    unsigned char* data = nullptr;
    if (XIGetProperty(dpy, deviceid, nodeAtom, 0, 1024, False, AnyPropertyType,
                      &type, &fmt, &nitems, &bytes, &data) != Success || !data)
        return {};
    std::string s(reinterpret_cast<char*>(data), nitems);
    XFree(data);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

void collectXSide(TouchReport& rep)
{
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        rep.note = "No X display; the X-server half of this report was skipped.";
        return;
    }
    int op = 0, ev = 0, err = 0;
    if (!XQueryExtension(dpy, "XInputExtension", &op, &ev, &err)) {
        rep.note = "XInput extension unavailable.";
        XCloseDisplay(dpy);
        return;
    }
    int major = 2, minor = 2;
    XIQueryVersion(dpy, &major, &minor);

    int n = 0;
    XIDeviceInfo* devs = XIQueryDevice(dpy, XIAllDevices, &n);
    for (int i = 0; i < n; ++i) {
        // Skip the master pointer: it inherits a touch class from its slaves and
        // would double-report the panel.
        if (devs[i].use == XIMasterPointer || devs[i].use == XIMasterKeyboard) continue;

        TouchXDevice xd;
        xd.id = devs[i].deviceid;
        xd.name = devs[i].name ? devs[i].name : "";
        for (int j = 0; j < devs[i].num_classes; ++j) {
            if (devs[i].classes[j]->type != XITouchClass) continue;
            const auto* t = reinterpret_cast<const XITouchClassInfo*>(devs[i].classes[j]);
            xd.hasTouchClass = true;
            xd.maxContacts = t->num_touches;
            xd.directMode = (t->mode == XIDirectTouch);
        }
        if (xd.name.find("wch.cn") == std::string::npos && !xd.hasTouchClass) continue;
        xd.eventNode = xEventNode(dpy, xd.id);
        rep.xdevices.push_back(std::move(xd));
    }
    if (devs) XIFreeDeviceInfo(devs);
    XCloseDisplay(dpy);
}

void buildVerdict(TouchReport& rep)
{
    if (!rep.haveDigitizer) {
        rep.verdict = "No Xeneon Edge digitizer (27c0:0859) found on this system.";
        return;
    }
    const TouchInterface* mt = nullptr;
    for (const auto& i : rep.interfaces)
        if (i.driver == "hid-multitouch") mt = &i;

    int advertised = 0;
    for (const auto& x : rep.xdevices)
        if (x.hasTouchClass && x.maxContacts > advertised) advertised = x.maxContacts;

    std::ostringstream os;
    if (mt && advertised > 1) {
        os << "MULTI-TOUCH ACTIVE. The kernel bound hid-multitouch to the digitizer "
              "interface (" << mt->hidId << ") and the X server advertises "
           << advertised << " simultaneous contacts.";
    } else if (mt) {
        os << "hid-multitouch is bound to " << mt->hidId
           << ", but no X touch class was found. Check that the X server is running "
              "and that libinput picked the device up.";
    } else {
        os << "SINGLE-CONTACT ONLY. No interface is bound to hid-multitouch, so the "
              "panel is being driven as an absolute mouse. Multi-touch will not work "
              "until hid-multitouch claims the digitizer interface.";
    }
    rep.verdict = os.str();
    if (rep.note.empty())
        rep.note = "Run `edgeline touch --live [seconds]` to measure the contacts the "
                   "panel really delivers under your fingers.";
}

} // namespace

TouchReport TouchProbe::run()
{
    TouchReport rep;
    collectKernelSide(rep);
    collectXSide(rep);
    buildVerdict(rep);
    return rep;
}

int TouchProbe::capturePeak(int seconds, long* beginCount, std::string* error)
{
    auto fail = [&](const char* m) {
        if (error) *error = m;
        return -1;
    };
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return fail("cannot open X display");

    int op = 0, ev = 0, err = 0;
    if (!XQueryExtension(dpy, "XInputExtension", &op, &ev, &err)) {
        XCloseDisplay(dpy);
        return fail("XInput extension unavailable");
    }
    int major = 2, minor = 2;
    if (XIQueryVersion(dpy, &major, &minor) != Success || major * 10 + minor < 22) {
        XCloseDisplay(dpy);
        return fail("XInput 2.2 or newer is required for touch events");
    }

    // Find the slave device that actually carries an XITouchClass. Selecting
    // XIAllDevices also delivers the master pointer's forwarded copy of each
    // touch, so counting every event would double every contact.
    int target = -1;
    int advertised = 0;
    {
        int nd = 0;
        XIDeviceInfo* di = XIQueryDevice(dpy, XIAllDevices, &nd);
        for (int i = 0; i < nd; ++i) {
            if (di[i].use == XIMasterPointer || di[i].use == XIMasterKeyboard) continue;
            for (int j = 0; j < di[i].num_classes; ++j) {
                if (di[i].classes[j]->type != XITouchClass) continue;
                const auto* t = reinterpret_cast<const XITouchClassInfo*>(di[i].classes[j]);
                if (target < 0) { target = di[i].deviceid; advertised = t->num_touches; }
            }
        }
        if (di) XIFreeDeviceInfo(di);
    }
    if (target < 0) {
        XCloseDisplay(dpy);
        return fail("no touch-capable device found");
    }

    std::array<unsigned char, XIMaskLen(XI_LASTEVENT)> maskBits{};
    XISetMask(maskBits.data(), XI_RawTouchBegin);
    XISetMask(maskBits.data(), XI_RawTouchUpdate);
    XISetMask(maskBits.data(), XI_RawTouchEnd);
    XIEventMask em;
    // XIAllDevices, not XIAllMasterDevices: the Edge's touch device may be
    // floating (Independent / Indicator touch modes detach it from every
    // master pointer), and a floating device delivers no raw events through
    // the master selection. TouchEventSource selects the same way.
    em.deviceid = XIAllDevices;
    em.mask_len = static_cast<int>(maskBits.size());
    em.mask = maskBits.data();
    XISelectEvents(dpy, DefaultRootWindow(dpy), &em, 1);
    XFlush(dpy);

    std::vector<std::pair<int, int>> active;  // (source device, touch id)
    int peak = 0;
    long begins = 0;
    const int fd = ConnectionNumber(dpy);
    const time_t start = std::time(nullptr);

    while (std::time(nullptr) - start < seconds) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(fd, &rd);
        timeval tv{ 0, 200000 };
        select(fd + 1, &rd, nullptr, nullptr, &tv);

        while (XPending(dpy)) {
            XEvent e;
            XNextEvent(dpy, &e);
            if (e.type != GenericEvent || e.xcookie.extension != op) continue;
            if (!XGetEventData(dpy, &e.xcookie)) continue;

            auto* re = reinterpret_cast<XIRawEvent*>(e.xcookie.data);
            // sourceid is the slave that physically produced the event; the
            // master forwards a copy with the same detail. Count the slave only.
            const int src = re->sourceid ? re->sourceid : re->deviceid;
            if (src != target) {
                XFreeEventData(dpy, &e.xcookie);
                continue;
            }
            const std::pair<int, int> key{ src, re->detail };
            if (e.xcookie.evtype == XI_RawTouchBegin) {
                if (std::find(active.begin(), active.end(), key) == active.end()) {
                    active.push_back(key);
                    ++begins;
                    if (static_cast<int>(active.size()) > peak)
                        peak = static_cast<int>(active.size());
                }
            } else if (e.xcookie.evtype == XI_RawTouchEnd) {
                auto it = std::find(active.begin(), active.end(), key);
                if (it != active.end()) active.erase(it);
            }
            XFreeEventData(dpy, &e.xcookie);
        }
    }
    XCloseDisplay(dpy);
    if (beginCount) *beginCount = begins;
    // A peak above what the device advertises means the counter is wrong, not
    // that the panel exceeded its own limit. Say so rather than printing it.
    if (advertised > 0 && peak > advertised) {
        if (error)
            *error = "counted " + std::to_string(peak) + " contacts but the device "
                     "advertises only " + std::to_string(advertised)
                   + "; the capture is double-counting";
        return -1;
    }
    return peak;
}

} // namespace xen
