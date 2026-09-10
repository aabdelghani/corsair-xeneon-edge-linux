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
#include <QDesktopServices>
#include <QGuiApplication>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QUrl>
#include <QVBoxLayout>

namespace xen {

DevicePage::DevicePage(QWidget* parent)
    : QWidget(parent), m_touch(new TouchControl(this))
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
        xen::TouchControl::applyOutputMapping();
        m_touch->refresh();
    });
    btnRow->addWidget(remapBtn);
    btnRow->addStretch(1);
    v->addLayout(btnRow);

    v->addSpacing(4);
    m_autostart = new QCheckBox(
        tr("Reapply this touch mode and display settings automatically at login"), card);
    m_autostart->setChecked(settings::autostartEnabled());
    connect(m_autostart, &QCheckBox::toggled, this, [](bool on) {
        settings::setAutostart(on);
    });
    v->addWidget(m_autostart);

    lay->addWidget(card);
    lay->addWidget(buildUpdatesCard(this));
    lay->addStretch(1);

    
    connect(m_touch, &TouchControl::stateChanged, this, &DevicePage::onTouchState);

    // Restore the last-used touch mode at startup (persists across reboots).
    restoreSavedMode();
}

QWidget* DevicePage::buildUpdatesCard(QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("card"));
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(20, 16, 20, 16);
    v->setSpacing(10);

    auto* title = new QLabel(tr("UPDATES"), card);
    title->setObjectName(QStringLiteral("cardTitle"));
    v->addWidget(title);

    // Say exactly what the network access is, where it goes, and how often.
    // A user should never have to read the source to find out that a desktop
    // app talks to the internet.
    auto* sub = new QLabel(
        tr("Checks github.com once a day for a newer release, and tells you if "
           "there is one. It sends nothing but the request itself, downloads "
           "nothing, and installs nothing."),
        card);
    sub->setObjectName(QStringLiteral("cardSubtitle"));
    sub->setWordWrap(true);
    v->addWidget(sub);

    m_updateAuto = new QCheckBox(tr("Check for updates automatically"), card);
    m_updateAuto->setChecked(UpdateChecker::enabled());
    connect(m_updateAuto, &QCheckBox::toggled, this, [](bool on) {
        UpdateChecker::setEnabled(on);
    });
    v->addWidget(m_updateAuto);

    auto* row = new QHBoxLayout;
    row->setSpacing(12);

    m_updateNow = new QPushButton(tr("Check now"), card);
    m_updateNow->setObjectName(QStringLiteral("actionButton"));
    m_updateNow->setCursor(Qt::PointingHandCursor);
    row->addWidget(m_updateNow, 0);

    m_updateStatus = new QLabel(
        tr("Running version %1.").arg(QStringLiteral(XENEON_VERSION)), card);
    m_updateStatus->setObjectName(QStringLiteral("cardSubtitle"));
    m_updateStatus->setWordWrap(true);
    row->addWidget(m_updateStatus, 1);
    v->addLayout(row);

    m_updates = new UpdateChecker(this);
    connect(m_updates, &UpdateChecker::updateAvailable, this,
            [this](const QString& version, const QString& url) {
                m_updateNow->setEnabled(true);
                m_updateStatus->setText(
                    tr("Version %1 is available. Opening the release page.").arg(version));
                QDesktopServices::openUrl(QUrl(url));
            });
    connect(m_updates, &UpdateChecker::upToDate, this, [this](const QString& version) {
        m_updateNow->setEnabled(true);
        m_updateStatus->setText(tr("Version %1 is the latest release.").arg(version));
    });
    connect(m_updates, &UpdateChecker::checkFailed, this, [this](const QString& err) {
        m_updateNow->setEnabled(true);
        m_updateStatus->setText(tr("Could not check: %1").arg(err));
    });
    connect(m_updateNow, &QPushButton::clicked, this, [this]() {
        m_updateNow->setEnabled(false);
        m_updateStatus->setText(tr("Checking..."));
        m_updates->check(true);
    });

    return card;
}

void DevicePage::showEvent(QShowEvent* ev)
{
    QWidget::showEvent(ev);
    syncMode();
    m_touch->refresh();
}

void DevicePage::syncMode()
{
    const TouchControl::Mode m = xen::TouchControl::mode();
    QRadioButton* b = m == TouchControl::Mode::Off        ? m_modeOff
                    : m == TouchControl::Mode::MainCursor  ? m_modeMain
                    : m == TouchControl::Mode::Independent ? m_modeIndep
                                                           : m_modeIndicator;
    QSignalBlocker const block(b);
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
