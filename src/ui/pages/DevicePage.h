// xeneon-ctl — Device page: touch/input controls (and HID features from M5).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/TouchControl.h"

#include <QCheckBox>
#include <QLabel>
#include <QPointer>
#include <QRadioButton>
#include <QWidget>

namespace xen {
class TouchEventSource;
class TouchIndicatorOverlay;
}

namespace xen {

class DevicePage : public QWidget {
    Q_OBJECT
public:
    explicit DevicePage(QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* ev) override;

private:
    void onTouchState(TouchControl::State state, const QString& detail);
    void showTouchIndicator();
    void calibrateTouch();
    void syncMode();
    void applyMode(TouchControl::Mode m);
    void restoreSavedMode();
    void startRippleIndicator();
    void stopRippleIndicator();
    QScreen* edgeScreen();

    TouchControl* m_touch = nullptr;
    QRadioButton* m_modeOff = nullptr;
    QRadioButton* m_modeMain = nullptr;
    QRadioButton* m_modeIndep = nullptr;
    QRadioButton* m_modeIndicator = nullptr;
    QCheckBox* m_autostart = nullptr;
    QLabel* m_touchDetail = nullptr;

    TouchEventSource* m_touchSource = nullptr;
    QPointer<TouchIndicatorOverlay> m_ripple;
};

} // namespace xen
