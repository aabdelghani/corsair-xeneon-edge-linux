// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/MainWindow.h"

#include "ui/dashboard/DashboardWindow.h"
#include "ui/pages/DevicePage.h"
#include "ui/pages/DisplayPage.h"
#include "ui/pages/ProbePage.h"

#include <QApplication>
#include <QGuiApplication>
#include <QMenu>
#include <QPainter>
#include <QScreen>
#include <QSystemTrayIcon>

#include <QButtonGroup>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace xen {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("XENEON CTL"));
    resize(1100, 640);

    auto* central = new QWidget(this);
    auto* rootLay = new QHBoxLayout(central);
    rootLay->setContentsMargins(0, 0, 0, 0);
    rootLay->setSpacing(0);

    rootLay->addWidget(buildSidebar());

    auto* right = new QWidget(central);
    auto* rightLay = new QVBoxLayout(right);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(0);

    rightLay->addWidget(buildHeader());

    m_banner = new QLabel(right);
    m_banner->setObjectName(QStringLiteral("banner"));
    m_banner->setWordWrap(true);
    m_banner->setVisible(false);
    rightLay->addWidget(m_banner);

    m_ddc = new DdcClient(this);

    m_pages = new QStackedWidget(right);
    m_pages->addWidget(buildHomePage());
    m_pages->addWidget(new DisplayPage(m_ddc, right));
    m_pages->addWidget(new DevicePage(right));
    m_pages->addWidget(new ProbePage(right));
    rightLay->addWidget(m_pages, 1);

    rootLay->addWidget(right, 1);
    setCentralWidget(central);

    setupTray();

    m_device = new EdgeDevice(this);
    connect(m_device, &EdgeDevice::stateChanged, this, &MainWindow::onDeviceState);
    m_device->startPolling(2000);
    onDeviceState(m_device->state());

    m_ddc->start();
}

QWidget* MainWindow::buildSidebar()
{
    auto* bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("sidebar"));
    bar->setFixedWidth(220);

    auto* lay = new QVBoxLayout(bar);
    lay->setContentsMargins(16, 20, 16, 20);
    lay->setSpacing(6);

    auto* brand = new QLabel(QStringLiteral("XENEON <span style='color:#ece81a'>CTL</span>"), bar);
    brand->setObjectName(QStringLiteral("brand"));
    brand->setTextFormat(Qt::RichText);
    lay->addWidget(brand);
    lay->addSpacing(18);

    const struct { const char* label; int page; } items[] = {
        { QT_TR_NOOP("Home"),    0 },
        { QT_TR_NOOP("Display"), 1 },
        { QT_TR_NOOP("Device"),  2 },
        { QT_TR_NOOP("Probe"),   3 },
    };

    auto* group = new QButtonGroup(bar);
    group->setExclusive(true);
    for (const auto& it : items) {
        auto* btn = new QPushButton(tr(it.label), bar);
        btn->setObjectName(QStringLiteral("navButton"));
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        group->addButton(btn, it.page);
        lay->addWidget(btn);
        if (it.page == 0)
            btn->setChecked(true);
    }
    connect(group, &QButtonGroup::idClicked, this, [this](int page) {
        m_pages->setCurrentIndex(page);
        static const char* titles[] = { "Home", "Display", "Device", "Probe" };
        m_pageTitle->setText(tr(titles[page]));
    });

    lay->addStretch(1);

    auto* version = new QLabel(QStringLiteral("xeneon-ctl 0.1.0 — M1"), bar);
    version->setObjectName(QStringLiteral("versionLabel"));
    lay->addWidget(version);
    return bar;
}

QWidget* MainWindow::buildHeader()
{
    auto* header = new QFrame(this);
    header->setObjectName(QStringLiteral("header"));
    header->setFixedHeight(56);

    auto* lay = new QHBoxLayout(header);
    lay->setContentsMargins(24, 0, 24, 0);

    m_pageTitle = new QLabel(tr("Home"), header);
    m_pageTitle->setObjectName(QStringLiteral("pageTitle"));
    lay->addWidget(m_pageTitle);
    lay->addStretch(1);

    m_chip = new QLabel(tr("SEARCHING…"), header);
    m_chip->setObjectName(QStringLiteral("chip"));
    m_chip->setProperty("state", "off");
    lay->addWidget(m_chip);
    return header;
}

QWidget* MainWindow::buildHomePage()
{
    auto* page = new QWidget(this);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(24, 24, 24, 24);

    auto* card = new QFrame(page);
    card->setObjectName(QStringLiteral("card"));
    auto* cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(20, 16, 20, 16);

    auto* title = new QLabel(tr("CORSAIR XENEON EDGE"), card);
    title->setObjectName(QStringLiteral("cardTitle"));
    cardLay->addWidget(title);

    auto* sub = new QLabel(tr("14.5\" LCD touchscreen — 2560×720"), card);
    sub->setObjectName(QStringLiteral("cardSubtitle"));
    cardLay->addWidget(sub);
    cardLay->addSpacing(10);

    auto* form = new QFormLayout;
    form->setHorizontalSpacing(24);
    form->setVerticalSpacing(8);
    auto addRow = [&](const QString& k, QLabel*& out) {
        auto* key = new QLabel(k, card);
        key->setObjectName(QStringLiteral("formKey"));
        out = new QLabel(QStringLiteral("—"), card);
        out->setObjectName(QStringLiteral("formValue"));
        out->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(key, out);
    };
    addRow(tr("Product"), m_valProduct);
    addRow(tr("Serial"), m_valSerial);
    addRow(tr("HID node"), m_valPath);
    addRow(tr("HID access"), m_valAccess);
    cardLay->addLayout(form);

    cardLay->addSpacing(14);
    auto* dashBtn = new QPushButton(tr("Open dashboard on the Edge"), card);
    dashBtn->setObjectName(QStringLiteral("actionButton"));
    dashBtn->setCursor(Qt::PointingHandCursor);
    connect(dashBtn, &QPushButton::clicked, this, &MainWindow::openDashboard);
    cardLay->addWidget(dashBtn, 0, Qt::AlignLeft);

    lay->addWidget(card);
    lay->addStretch(1);
    return page;
}

void MainWindow::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    // Paint a small yellow "XE" badge so we do not need an asset file.
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(0x16, 0x17, 0x1a));
        p.setPen(QPen(QColor(0xEC, 0xE8, 0x1A), 4));
        p.drawRoundedRect(4, 4, 56, 56, 14, 14);
        QFont f = p.font();
        f.setBold(true);
        f.setPointSize(22);
        p.setFont(f);
        p.setPen(QColor(0xEC, 0xE8, 0x1A));
        p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("XE"));
    }
    const QIcon icon(pm);
    setWindowIcon(icon);

    m_tray = new QSystemTrayIcon(icon, this);
    m_tray->setToolTip(QStringLiteral("Xeneon Edge control"));

    auto* menu = new QMenu(this);
    menu->addAction(tr("Show window"), this, [this] { showNormal(); raise(); activateWindow(); });
    menu->addAction(tr("Open dashboard on the Edge"), this, &MainWindow::openDashboard);
    menu->addSeparator();
    menu->addAction(tr("Quit"), qApp, &QApplication::quit);
    m_tray->setContextMenu(menu);

    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason r) {
                if (r == QSystemTrayIcon::Trigger) {
                    if (isVisible())
                        hide();
                    else {
                        showNormal();
                        raise();
                        activateWindow();
                    }
                }
            });
    m_tray->show();
}

void MainWindow::openDashboard()
{
    QScreen* edge = nullptr;
    for (QScreen* s : QGuiApplication::screens()) {
        const QSize sz = s->geometry().size();
        if (sz.width() == 2560 && sz.height() == 720) {
            edge = s;
            break;
        }
    }
    if (!edge)
        edge = screen();
    auto* dash = new DashboardWindow;
    dash->setAttribute(Qt::WA_DeleteOnClose);
    dash->showOnScreen(edge);
}

QWidget* MainWindow::buildPlaceholderPage(const QString& title, const QString& note)
{
    auto* page = new QWidget(this);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(24, 24, 24, 24);

    auto* card = new QFrame(page);
    card->setObjectName(QStringLiteral("card"));
    auto* cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(20, 16, 20, 16);

    auto* t = new QLabel(title, card);
    t->setObjectName(QStringLiteral("cardTitle"));
    cardLay->addWidget(t);
    auto* n = new QLabel(note, card);
    n->setObjectName(QStringLiteral("cardSubtitle"));
    n->setWordWrap(true);
    cardLay->addWidget(n);

    lay->addWidget(card);
    lay->addStretch(1);
    return page;
}

void MainWindow::onDeviceState(const xen::EdgeDevice::State& s)
{
    if (!s.present) {
        m_chip->setText(tr("DISCONNECTED"));
        m_chip->setProperty("state", "off");
    } else if (!s.accessible) {
        m_chip->setText(tr("NO ACCESS"));
        m_chip->setProperty("state", "warn");
    } else {
        m_chip->setText(tr("XENEON EDGE — CONNECTED"));
        m_chip->setProperty("state", "ok");
    }
    // Re-polish so the [state=...] QSS selector re-applies
    style()->unpolish(m_chip);
    style()->polish(m_chip);

    m_banner->setVisible(s.present && !s.accessible);
    if (s.present && !s.accessible) {
        m_banner->setText(
            tr("The Edge is connected but %1 is not readable by your user. Install the udev rule:\n"
               "  sudo cp udev/60-corsair-xeneon.rules /etc/udev/rules.d/ && "
               "sudo udevadm control --reload && sudo udevadm trigger")
                .arg(s.path.isEmpty() ? QStringLiteral("/dev/hidraw*") : s.path));
    }

    m_valProduct->setText(s.present && !s.product.isEmpty() ? s.product : QStringLiteral("—"));
    m_valSerial->setText(s.present && !s.serial.isEmpty() ? s.serial : QStringLiteral("—"));
    m_valPath->setText(s.present && !s.path.isEmpty() ? s.path : QStringLiteral("—"));
    m_valAccess->setText(!s.present ? QStringLiteral("—")
                                    : (s.accessible ? tr("OK") : tr("denied")));
}

} // namespace xen
