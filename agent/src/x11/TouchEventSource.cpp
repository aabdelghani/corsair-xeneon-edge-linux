// SPDX-License-Identifier: GPL-3.0-or-later
#include "x11/TouchEventSource.h"

#include <QSocketNotifier>

#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>

#include <algorithm>
#include <cstring>

namespace xen {

namespace {
constexpr const char* kTouchName = "wch.cn TouchScreen";
inline Display* dpyOf(void* p) { return static_cast<Display*>(p); }
} // namespace

TouchEventSource::TouchEventSource(QObject* parent)
    : QObject(parent)
{
}

TouchEventSource::~TouchEventSource()
{
    stop();
}

bool TouchEventSource::start()
{
    if (m_dpy)
        return true;

    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        m_error = QStringLiteral("cannot open X display");
        return false;
    }

    int ev = 0;
    int err = 0;
    if (!XQueryExtension(dpy, "XInputExtension", &m_xiOpcode, &ev, &err)) {
        m_error = QStringLiteral("XInput extension missing");
        XCloseDisplay(dpy);
        return false;
    }
    int major = 2;
    int minor = 2;
    if (XIQueryVersion(dpy, &major, &minor) != Success || major * 10 + minor < 22) {
        m_error = QStringLiteral("XInput2 >= 2.2 required (touch)");
        XCloseDisplay(dpy);
        return false;
    }

    m_dpy = dpy;
    m_root = DefaultRootWindow(dpy);

    // Select raw touch events + hierarchy changes on the root.
    unsigned char maskBits[XIMaskLen(XI_LASTEVENT)];
    std::memset(maskBits, 0, sizeof maskBits);
    XISetMask(maskBits, XI_RawTouchBegin);
    XISetMask(maskBits, XI_RawTouchUpdate);
    XISetMask(maskBits, XI_RawTouchEnd);
    XISetMask(maskBits, XI_HierarchyChanged);
    XIEventMask em;
    em.deviceid = XIAllDevices;
    em.mask_len = sizeof maskBits;
    em.mask = maskBits;
    XISelectEvents(dpy, m_root, &em, 1);
    XFlush(dpy);

    queryDevices();

    m_notifier = new QSocketNotifier(ConnectionNumber(dpy), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &TouchEventSource::onReadable);
    return true;
}

void TouchEventSource::stop()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    if (m_dpy) {
        XCloseDisplay(dpyOf(m_dpy));
        m_dpy = nullptr;
    }
    m_devs.clear();
    m_lastByTouch.clear();
}

bool TouchEventSource::collectDev(const void* xiDeviceInfo, Dev& out)
{
    const auto& d = *static_cast<const XIDeviceInfo*>(xiDeviceInfo);
    Display* dpy = dpyOf(m_dpy);

    // Collect the absolute valuators, ordered by number.
    struct V {
        int number;
        double min, max;
        char axis; // 'x','y','?'
    };
    std::vector<V> vs;
    for (int c = 0; c < d.num_classes; ++c) {
        if (d.classes[c]->type != XIValuatorClass)
            continue;
        auto* vi = reinterpret_cast<XIValuatorClassInfo*>(d.classes[c]);
        char axis = '?';
        if (vi->label) {
            char* ln = XGetAtomName(dpy, vi->label);
            if (ln) {
                axis = std::strstr(ln, "Y") ? 'y' : (std::strstr(ln, "X") ? 'x' : '?');
                XFree(ln);
            }
        }
        vs.push_back({ vi->number, vi->min, vi->max, axis });
    }
    if (vs.size() < 2)
        return false;

    std::sort(vs.begin(), vs.end(), [](const V& a, const V& b) { return a.number < b.number; });
    // Prefer labelled axes; else the first two valuators are X, Y.
    V vx = vs[0];
    V vy = vs[1];
    for (const V& v : vs) {
        if (v.axis == 'x')
            vx = v;
        else if (v.axis == 'y')
            vy = v;
    }
    out.x = { vx.min, vx.max, vx.number };
    out.y = { vy.min, vy.max, vy.number };
    return true;
}

void TouchEventSource::queryDevices()
{
    m_devs.clear();
    Display* dpy = dpyOf(m_dpy);
    int n = 0;
    XIDeviceInfo* infos = XIQueryDevice(dpy, XIAllDevices, &n);
    for (int i = 0; i < n; ++i) {
        const XIDeviceInfo& d = infos[i];
        if (!d.name || std::strstr(d.name, kTouchName) == nullptr)
            continue;
        Dev dev;
        if (collectDev(&d, dev))
            m_devs.insert(d.deviceid, dev);
    }
    XIFreeDeviceInfo(infos);
}

void TouchEventSource::normalizeXY(void* rawEvent, const Dev& dev, QPointF& io)
{
    auto* re = static_cast<XIRawEvent*>(rawEvent);
    double rawX = 0;
    double rawY = 0;
    bool haveX = false;
    bool haveY = false;
    const double* val = re->raw_values;
    const int nbits = re->valuators.mask_len * 8;
    for (int b = 0; b < nbits; ++b) {
        if (!XIMaskIsSet(re->valuators.mask, b))
            continue;
        const double v = *val++;
        if (b == dev.x.number) {
            rawX = v;
            haveX = true;
        } else if (b == dev.y.number) {
            rawY = v;
            haveY = true;
        }
    }
    if (haveX && dev.x.max > dev.x.min)
        io.setX(std::clamp((rawX - dev.x.min) / (dev.x.max - dev.x.min), 0.0, 1.0));
    if (haveY && dev.y.max > dev.y.min)
        io.setY(std::clamp((rawY - dev.y.min) / (dev.y.max - dev.y.min), 0.0, 1.0));
}

void TouchEventSource::processRawTouch(void* rawEvent, int evtype)
{
    auto* re = static_cast<XIRawEvent*>(rawEvent);
    int devId = -1;
    if (m_devs.contains(re->deviceid))
        devId = re->deviceid;
    else if (m_devs.contains(re->sourceid))
        devId = re->sourceid;
    if (devId < 0)
        return;
    const Dev dev = m_devs.value(devId);
    const int touchId = re->detail;

    if (evtype == XI_RawTouchEnd) {
        // End often carries no position; reuse the last one.
        const QPointF last = m_lastByTouch.value(touchId, QPointF(-1, -1));
        m_lastByTouch.remove(touchId);
        if (last.x() >= 0)
            emit touch(touchId, Phase::End, last.x(), last.y());
        return;
    }

    QPointF norm = m_lastByTouch.value(touchId, QPointF(0.5, 0.5));
    normalizeXY(re, dev, norm);
    m_lastByTouch.insert(touchId, norm);
    const Phase phase = evtype == XI_RawTouchBegin ? Phase::Begin : Phase::Update;
    emit touch(touchId, phase, norm.x(), norm.y());
}

void TouchEventSource::onReadable()
{
    Display* dpy = dpyOf(m_dpy);
    while (XPending(dpy)) {
        XEvent xe;
        XNextEvent(dpy, &xe);
        if (xe.type != GenericEvent || xe.xcookie.extension != m_xiOpcode)
            continue;
        if (!XGetEventData(dpy, &xe.xcookie))
            continue;

        const int type = xe.xcookie.evtype;
        if (type == XI_HierarchyChanged)
            queryDevices();
        else if (type == XI_RawTouchBegin || type == XI_RawTouchUpdate || type == XI_RawTouchEnd)
            processRawTouch(xe.xcookie.data, type);

        XFreeEventData(dpy, &xe.xcookie);
    }
}

} // namespace xen
