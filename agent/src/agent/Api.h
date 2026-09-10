// edgeline agent: the RPC surface.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One object owns every device and every cache. The UI holds no hardware state
// of its own; it asks for a snapshot on connect and then follows events. That
// keeps two windows (the control window and the panel dashboard) from fighting
// over the same panel, which is the thing a shared-nothing UI cannot do.
#pragma once

#include "core/ColorProfiles.h"
#include "core/DdcCapabilities.h"
#include "core/RulesStore.h"
#include "core/DdcClient.h"
#include "core/EdgeDevice.h"
#include "core/SensorSource.h"
#include "core/TouchControl.h"
#include "core/UpdateChecker.h"
#include "ipc/RpcServer.h"

#include <QJsonArray>
#include <QMap>
#include <QObject>
#include <QStringList>

namespace xen {

class TouchEventSource;
class FocusWatcher;

class Api : public QObject {
    Q_OBJECT
public:
    explicit Api(RpcServer* rpc, QObject* parent = nullptr);

    // Kick off device discovery. Safe to call with nothing connected.
    void start();

private:
    void registerMethods();

    // Snapshots, shared by the *.state methods and by the events that follow.
    [[nodiscard]] QJsonObject ddcSnapshot() const;
    [[nodiscard]] QJsonObject touchSnapshot() const;
    [[nodiscard]] QJsonObject deviceSnapshot() const;
    [[nodiscard]] QJsonObject sensorSnapshot() const;
    [[nodiscard]] QJsonObject rulesSnapshot() const;
    [[nodiscard]] QJsonObject colorSnapshot() const;
    [[nodiscard]] QJsonObject focusSnapshot() const;
    [[nodiscard]] QJsonObject systemSnapshot() const;
    [[nodiscard]] QJsonObject captureProfile() const;
    [[nodiscard]] QString profileSummary(const QJsonObject& body) const;
    [[nodiscard]] QStringList missingPictureValues() const;
    [[nodiscard]] bool isUserPreset(int presetCode) const;

    void wireSignals();
    void appendDdcLog(const QString& line);
    void setTouchStreaming(bool on);
    void setRulesActive(bool on);
    void onFocusChanged(const WindowInfo& win);
    // The single path a profile is applied through, so a rule firing and a
    // click in the UI cannot diverge.
    bool applyProfile(const QString& name, QJsonObject& result, QString& error);
    static std::vector<ColorDevice> readColorDevices();

    static QString toolVersion(const QString& exe, const QStringList& args);
    static QString toolPath(const QString& exe);

    RpcServer* m_rpc = nullptr;
    DdcClient* m_ddc = nullptr;
    TouchControl* m_touch = nullptr;
    EdgeDevice* m_device = nullptr;
    SensorSource* m_sensors = nullptr;
    UpdateChecker* m_updates = nullptr;
    TouchEventSource* m_touchStream = nullptr;
    FocusWatcher* m_focus = nullptr;

    rules::Config m_rules;
    WindowInfo m_focused;
    // The profile that was active before a rule took over, so unfocusing can
    // put it back rather than leaving the panel wherever the last app left it.
    QString m_profileBeforeRule;
    QString m_ruleAppliedProfile;

    // DDC cache. The old Qt app read each value exactly once per run and kept
    // no cache, so a second window would have had nothing to render.
    struct VcpState {
        int current = -1;
        int max = 0;
    };
    QMap<int, VcpState> m_vcp;
    DdcCapabilities m_caps;
    bool m_capsFetched = false;
    bool m_ddcReady = false;
    QString m_ddcMessage;

    QStringList m_ddcLog;      // most recent lines, newest last
    bool m_touchStreaming = false;
    SensorSnapshot m_snap;
    bool m_sensorStreaming = false;
};

} // namespace xen
