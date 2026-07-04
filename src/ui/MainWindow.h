// xeneon-ctl — main application window (iCUE-dark shell).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/DdcClient.h"
#include "core/EdgeDevice.h"

#include <QLabel>
#include <QMainWindow>
#include <QPointer>
#include <QPushButton>
#include <QStackedWidget>
#include <QSystemTrayIcon>

namespace xen {

class DashboardWindow;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onDeviceState(const xen::EdgeDevice::State& s);
    void openDashboard();

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void setupTray();
    void quitApp();

private:
    QWidget* buildSidebar();
    QWidget* buildHeader();
    QWidget* buildHomePage();
    QWidget* buildPlaceholderPage(const QString& title, const QString& note);

    EdgeDevice* m_device = nullptr;
    DdcClient* m_ddc = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QStackedWidget* m_pages = nullptr;
    QLabel* m_pageTitle = nullptr;
    QLabel* m_chip = nullptr;
    QLabel* m_banner = nullptr;

    // Home page detail labels
    QLabel* m_valProduct = nullptr;
    QLabel* m_valSerial = nullptr;
    QLabel* m_valPath = nullptr;
    QLabel* m_valAccess = nullptr;

    QPushButton* m_dashBtn = nullptr;
    QPointer<DashboardWindow> m_dash;

    bool m_forceQuit = false;
    bool m_trayHintShown = false;
};

} // namespace xen
