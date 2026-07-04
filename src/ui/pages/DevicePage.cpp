// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/pages/DevicePage.h"

#include "core/AppSettings.h"
#include "ui/CalibrationOverlay.h"
#include "ui/TouchIndicatorOverlay.h"
#include "ui/TouchOverlay.h"
#include "x11/TouchEventSource.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QFrame>
#include <QGuiApplication>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace xen {

DevicePage::DevicePage(QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(24, 24, 24, 24);
    lay->setSpacing(16);

    // --- Touch input card ---
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("card"));
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(20, 16, 20, 16);
    v->setSpacing(8);

    auto* title = new QLabel(tr("TOUCH INPUT"), card);
    title->setObjectName(QStringLiteral("cardTitle"));
    v->addWidget(title);

    m_modeOff = new QRadioButton(tr("Off — touch does nothing"), card);
    m_modeMain = new QRadioButton(tr("Move the main mouse cursor (touch jumps your cursor to the Edge)"), card);
    m_modeIndep = new QRadioButton(
        tr("Independent — touch controls the Edge only (shows a second cursor on the Edge)"), card);
    m_modeIndicator = new QRadioButton(
        tr("Indicator — no pointer, just a ripple where you touch (recommended)"), card);
    auto* group = new QButtonGroup(this);
    group->addButton(m_modeOff, int(TouchControl::Mode::Off));
    group->addButton(m_modeMain, int(TouchControl::Mode::MainCursor));
    group->addButton(m_modeIndep, int(TouchControl::Mode::Independent));
    group->addButton(m_modeIndicator, int(TouchControl::Mode::Indicator));
    v->addWidget(m_modeOff);
    v->addWidget(m_modeMain);
    v->addWidget(m_modeIndep);
    v->addWidget(m_modeIndicator);
    connect(group, &QButtonGroup::idClicked, this, [this](int id) {
        applyMode(static_cast<TouchControl::Mode>(id));
    });

    m_touchDetail = new QLabel(card);
    m_touchDetail->setObjectName(QStringLiteral("cardSubtitle"));
    m_touchDetail->setWordWrap(true);
    v->addWidget(m_touchDetail);

    v->addSpacing(6);
    auto* btnRow = new QHBoxLayout;
    auto* indicatorBtn = new QPushButton(tr("Show touch indicator"), card);
    indicatorBtn->setObjectName(QStringLiteral("actionButton"));
    connect(indicatorBtn, &QPushButton::clicked, this, [this] { showTouchIndicator(); });
    btnRow->addWidget(indicatorBtn);

    auto* calibrateBtn = new QPushButton(tr("Calibrate touch…"), card);
    calibrateBtn->setObjectName(QStringLiteral("actionButton"));
    connect(calibrateBtn, &QPushButton::clicked, this, [this] { calibrateTouch(); });
    btnRow->addWidget(calibrateBtn);

    auto* remapBtn = new QPushButton(tr("Reset mapping"), card);
    remapBtn->setObjectName(QStringLiteral("actionButton"));
    connect(remapBtn, &QPushButton::clicked, this, [this] {
        m_touch->applyOutputMapping();
        m_touch->refresh();
    });
    btnRow->addWidget(remapBtn);
    btnRow->addStretch(1);
    v->addLayout(btnRow);

    v->addSpacing(4);
    m_autostart = new QCheckBox(
        tr("Reapply this touch mode and display settings automatically at login"), card);
    m_autostart->setChecked(settings::autostartEnabled());
    connect(m_autostart, &QCheckBox::toggled, this, [this](bool on) {
        settings::setAutostart(on);
    });
    v->addWidget(m_autostart);

    lay->addWidget(card);
    lay->addStretch(1);

    m_touch = new TouchControl(this);
    connect(m_touch, &TouchControl::stateChanged, this, &DevicePage::onTouchState);

    // Restore the last-used touch mode at startup (persists across reboots).
    restoreSavedMode();
}

void DevicePage::showEvent(QShowEvent* ev)
{
    QWidget::showEvent(ev);
    syncMode();
    m_touch->refresh();
}

void DevicePage::syncMode()
{
    const TouchControl::Mode m = m_touch->mode();
    QRadioButton* b = m == TouchControl::Mode::Off        ? m_modeOff
                    : m == TouchControl::Mode::MainCursor  ? m_modeMain
                    : m == TouchControl::Mode::Independent ? m_modeIndep
                                                           : m_modeIndicator;
    QSignalBlocker block(b);
    b->setChecked(true);
}

QScreen* DevicePage::edgeScreen()
{
    for (QScreen* s : QGuiApplication::screens()) {
        const QSize sz = s->geometry().size();
        if (sz.width() == 2560 && sz.height() == 720)
            return s;
    }
    return screen();
}

void DevicePage::applyMode(TouchControl::Mode m)
{
    settings::saveTouchMode(int(m));
    if (m == TouchControl::Mode::Indicator) {
        m_touch->setMode(m);       // float the devices (no pointer)
        startRippleIndicator();
    } else {
        stopRippleIndicator();
        m_touch->setMode(m);
    }
}

void DevicePage::restoreSavedMode()
{
    const int saved = settings::loadTouchMode(-1);
    if (saved >= 0)
        applyMode(static_cast<TouchControl::Mode>(saved));
    else
        syncMode();
}

void DevicePage::startRippleIndicator()
{
    if (!m_touchSource) {
        m_touchSource = new TouchEventSource(this);
        if (!m_touchSource->start()) {
            m_touchDetail->setText(tr("Touch indicator unavailable: %1")
                                       .arg(m_touchSource->lastError()));
            delete m_touchSource;
            m_touchSource = nullptr;
            return;
        }
    }
    if (!m_ripple) {
        m_ripple = new TouchIndicatorOverlay;
        connect(m_touchSource, &TouchEventSource::touch, m_ripple.data(),
                &TouchIndicatorOverlay::onTouch);
        m_ripple->showOnScreen(edgeScreen());
    }
}

void DevicePage::stopRippleIndicator()
{
    if (m_ripple)
        m_ripple->close(); // WA_DeleteOnClose
    m_ripple = nullptr;
    if (m_touchSource) {
        m_touchSource->stop();
        m_touchSource->deleteLater();
        m_touchSource = nullptr;
    }
}

void DevicePage::showTouchIndicator()
{
    auto* overlay = new TouchOverlay;
    overlay->showOnScreen(edgeScreen());
}

void DevicePage::calibrateTouch()
{
    auto* cal = new CalibrationOverlay(m_touch);
    cal->showOnScreen(edgeScreen());
}

void DevicePage::onTouchState(TouchControl::State state, const QString& detail)
{
    Q_UNUSED(state);
    m_touchDetail->setText(detail);
    syncMode();
}

} // namespace xen
