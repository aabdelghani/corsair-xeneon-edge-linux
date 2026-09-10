// edgeline: enable/disable the Edge's touchscreen acting as a mouse pointer.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Edge's capacitive panel enumerates as X11 pointer devices named
// "wch.cn TouchScreen". Disabling them stops touches from moving the cursor
// (useful when the Edge shows a dashboard you don't want to poke the desktop
// with). Uses `xinput`; a no-op with a clear message under Wayland.
#pragma once

#include <QObject>
#include <QString>

namespace xen {

class TouchControl : public QObject {
    Q_OBJECT
public:
    explicit TouchControl(QObject* parent = nullptr);

    enum class State { Unknown, Enabled, Disabled, NotFound, Unsupported };

    // How the Edge's touchscreen behaves:
    //   Off         - touch does nothing
    //   MainCursor  - touch drives the system mouse cursor (jumps your cursor)
    //   Independent - touch drives its own cursor confined to the Edge, so the
    //                 main mouse is left alone (X11 multi-pointer)
    //   Indicator   - touch is floated (drives no pointer at all); a ripple
    //                 overlay shows where you touched
    enum class Mode { Off, MainCursor, Independent, Indicator };

    // Re-reads xinput and reports whether the touch devices move the pointer.
    State refresh();
    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] QString detail() const { return m_detail; }

    static Mode mode();                 // current mode from xinput topology
    bool setMode(Mode m);        // apply a mode

    // true => touch moves the pointer; false => touch is disabled.
    bool setPointerEnabled(bool enabled);

    // Coordinate Transformation Matrix (3x3, row-major) of the touch devices.
    // Returns the first device's matrix; identity on failure.
    static QList<double> matrix();
    // Applies map-to-output DP-<edge> baseline (geometric mapping).
    static bool applyOutputMapping();
    // Sets an explicit 3x3 matrix on all touch devices (for calibration).
    static bool setMatrix(const QList<double>& m);

    static QList<int> touchDeviceIds() { return deviceIds(); }

signals:
    void stateChanged(xen::TouchControl::State state, const QString& detail);

private:
    static QList<int> deviceIds();       // xinput ids of "wch.cn TouchScreen" devices
    static bool deviceMovesPointer(int id);
    static int masterPointerId();        // "Virtual core pointer" id
    static int deviceMasterId(int id);   // master this slave is attached to (-1 floating)
    static int ensureEdgeMaster();       // id of "xeneon-edge pointer", creating if needed
    static void removeEdgeMasterIfEmpty();
    static QString edgeOutputName();     // xrandr output showing the 2560x720 Edge
    [[nodiscard]] static bool isX11() ;

    State m_state = State::Unknown;
    QString m_detail;
};

} // namespace xen
