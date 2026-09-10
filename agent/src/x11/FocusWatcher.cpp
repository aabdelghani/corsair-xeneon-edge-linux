// SPDX-License-Identifier: GPL-3.0-or-later
#include "x11/FocusWatcher.h"

#include <QSocketNotifier>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace xen {
namespace {

Display* dpyOf(void* p) { return static_cast<Display*>(p); }

// Read a window-id property such as _NET_ACTIVE_WINDOW.
Window readWindowProperty(Display* dpy, Window from, Atom prop)
{
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long nItems = 0, bytesAfter = 0;
    unsigned char* data = nullptr;
    Window out = None;

    if (XGetWindowProperty(dpy, from, prop, 0, 1, False, AnyPropertyType,
                           &actualType, &actualFormat, &nItems, &bytesAfter, &data)
            == Success
        && data) {
        if (nItems > 0 && actualFormat == 32)
            out = *reinterpret_cast<Window*>(data);
        XFree(data);
    }
    return out;
}

std::vector<std::string> readAtomList(Display* dpy, Window w, Atom prop)
{
    std::vector<std::string> out;
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long nItems = 0, bytesAfter = 0;
    unsigned char* data = nullptr;

    if (XGetWindowProperty(dpy, w, prop, 0, 64, False, XA_ATOM,
                           &actualType, &actualFormat, &nItems, &bytesAfter, &data)
            != Success
        || !data)
        return out;

    auto* atoms = reinterpret_cast<Atom*>(data);
    for (unsigned long i = 0; i < nItems; ++i) {
        if (char* name = XGetAtomName(dpy, atoms[i])) {
            out.emplace_back(name);
            XFree(name);
        }
    }
    XFree(data);
    return out;
}

} // namespace

FocusWatcher::FocusWatcher(QObject* parent)
    : QObject(parent)
{
}

FocusWatcher::~FocusWatcher() { stop(); }

bool FocusWatcher::start()
{
    if (m_dpy)
        return true;

    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        m_error = QStringLiteral("cannot open an X display (rules need X11)");
        return false;
    }
    m_dpy = dpy;
    m_root = DefaultRootWindow(dpy);

    // Property changes on the root are how _NET_ACTIVE_WINDOW arrives.
    XSelectInput(dpy, m_root, PropertyChangeMask);
    XFlush(dpy);

    m_notifier = new QSocketNotifier(ConnectionNumber(dpy), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &FocusWatcher::onReadable);

    readActive();
    return true;
}

void FocusWatcher::stop()
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
    m_current = WindowInfo{};
}

void FocusWatcher::onReadable()
{
    Display* dpy = dpyOf(m_dpy);
    bool interesting = false;
    while (XPending(dpy)) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (ev.type == PropertyNotify && ev.xproperty.window == m_root)
            interesting = true;
    }
    if (interesting)
        readActive();
}

void FocusWatcher::readActive()
{
    Display* dpy = dpyOf(m_dpy);
    if (!dpy)
        return;

    const Atom activeAtom = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", True);
    if (activeAtom == None)
        return;

    WindowInfo info;
    const Window active = readWindowProperty(dpy, m_root, activeAtom);
    if (active != None) {
        // A window can vanish between being named active and being read, which
        // would otherwise kill the connection with a BadWindow.
        XErrorHandler old = XSetErrorHandler([](Display*, XErrorEvent*) { return 0; });

        XClassHint hint{};
        if (XGetClassHint(dpy, active, &hint)) {
            if (hint.res_name) { info.wmInstance = hint.res_name; XFree(hint.res_name); }
            if (hint.res_class) { info.wmClass = hint.res_class; XFree(hint.res_class); }
            info.valid = true;
        }
        if (const Atom stateAtom = XInternAtom(dpy, "_NET_WM_STATE", True); stateAtom != None)
            info.states = readAtomList(dpy, active, stateAtom);

        XSync(dpy, False);
        XSetErrorHandler(old);
    }

    // Only report real transitions, so a rule does not re-fire every time some
    // unrelated root property changes.
    if (info.valid == m_current.valid
        && info.wmInstance == m_current.wmInstance
        && info.wmClass == m_current.wmClass
        && info.states == m_current.states)
        return;

    m_current = info;
    emit focusChanged(m_current);
}

} // namespace xen
