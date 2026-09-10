// edgeline: raw touch reader for the Edge via XInput2 (M5.3).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Opens its own Xlib display and selects XI raw-touch events so it can read the
// Edge's touchscreen even when the device is floating (drives no pointer). The
// X connection fd is folded into Qt's event loop with a QSocketNotifier — no
// worker thread. Emits normalized (0..1) touch positions.
#pragma once

#include <QHash>
#include <QObject>
#include <QPointF>

class QSocketNotifier;

namespace xen {

class TouchEventSource : public QObject {
    Q_OBJECT
public:
    enum class Phase { Begin, Update, End };

    explicit TouchEventSource(QObject* parent = nullptr);
    ~TouchEventSource() override;

    // Opens the X display, finds the touch device(s), selects raw touch events.
    bool start();
    void stop();
    [[nodiscard]] bool running() const { return m_dpy != nullptr; }
    [[nodiscard]] QString lastError() const { return m_error; }

signals:
    void touch(int id, xen::TouchEventSource::Phase phase, double nx, double ny);

private:
    struct AxisRange {
        double min = 0;
        double max = 1;
        int number = -1;
    };
    struct Dev {
        AxisRange x;
        AxisRange y;
    };

    void onReadable();
    void queryDevices();                              // (re)discover wch.cn valuator ranges
    bool collectDev(const void* xiDeviceInfo, Dev& out); // fill Dev from an XIDeviceInfo*
    void processRawTouch(void* rawEvent, int evtype);    // handle one XIRawEvent
    static void normalizeXY(void* rawEvent, const Dev& dev, QPointF& io); // raw -> 0..1

    void* m_dpy = nullptr;         // Display* (opaque to keep Xlib out of header)
    int m_xiOpcode = 0;
    unsigned long m_root = 0;
    QSocketNotifier* m_notifier = nullptr;
    QHash<int, Dev> m_devs;        // deviceid -> valuator ranges
    QHash<int, QPointF> m_lastByTouch; // touchid -> last normalized pos (for End)
    QString m_error;
};

} // namespace xen
