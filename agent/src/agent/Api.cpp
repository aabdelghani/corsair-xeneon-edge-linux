// SPDX-License-Identifier: GPL-3.0-or-later
#include "agent/Api.h"

#include "core/AppSettings.h"
#include "core/Calibration.h"
#include "x11/TouchEventSource.h"
#include "x11/TouchProbe.h"

#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>

namespace xen {
namespace {

constexpr int kDdcLogLines = 40;

// The panel's own restore-defaults features. Writing 1 triggers them.
constexpr int kRestoreFactory = 0x04;
constexpr int kRestoreBrightness = 0x05;
constexpr int kRestoreColor = 0x08;

QJsonArray toArray(const QList<double>& v)
{
    QJsonArray a;
    for (double d : v)
        a.append(d);
    return a;
}

std::vector<CalPoint> pointsFrom(const QJsonArray& arr)
{
    std::vector<CalPoint> out;
    out.reserve(size_t(arr.size()));
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        out.push_back(CalPoint{ o.value(QStringLiteral("x")).toDouble(),
                                o.value(QStringLiteral("y")).toDouble() });
    }
    return out;
}

} // namespace

Api::Api(RpcServer* rpc, QObject* parent)
    : QObject(parent)
    , m_rpc(rpc)
    , m_ddc(new DdcClient(this))
    , m_touch(new TouchControl(this))
    , m_device(new EdgeDevice(this))
    , m_sensors(new SensorSource(this))
    , m_updates(new UpdateChecker(this))
{
    registerMethods();
    wireSignals();
}

void Api::start()
{
    m_ddc->start();
    m_device->startPolling(2000);
    m_touch->refresh();
}

QString Api::toolVersion(const QString& exe, const QStringList& args)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(exe, args);
    if (!p.waitForStarted(1500) || !p.waitForFinished(3000))
        return {};
    const QString out = QString::fromUtf8(p.readAll());
    // Both ddcutil and xinput put a bare dotted version somewhere in line one.
    static const QRegularExpression re(QStringLiteral("(\\d+\\.\\d+(?:\\.\\d+)?)"));
    const auto m = re.match(out);
    return m.hasMatch() ? m.captured(1) : QString();
}

void Api::wireSignals()
{
    connect(m_ddc, &DdcClient::readyChanged, this, [this](bool ready, const QString& msg) {
        m_ddcReady = ready;
        m_ddcMessage = msg;
        if (ready && !m_capsFetched) {
            m_capsFetched = true;
            m_ddc->fetchCapabilities();
            // Seed the cache so a UI connecting later has real values, not
            // placeholders, without having to ask for each one.
            for (int code : { 0x10, 0x12, 0x14, 0x16, 0x18, 0x1A, 0x87, 0x60, 0xD6 })
                m_ddc->getVcp(quint8(code));
        }
        m_rpc->broadcast(QStringLiteral("ddc"), ddcSnapshot());
    });

    connect(m_ddc, &DdcClient::capabilitiesRead, this, [this](const QString& raw) {
        m_caps = parseCapabilities(raw.toStdString());
        m_rpc->broadcast(QStringLiteral("ddc"), ddcSnapshot());
    });

    connect(m_ddc, &DdcClient::vcpRead, this, [this](quint8 code, quint16 cur, quint16 max) {
        VcpState& st = m_vcp[code];
        st.current = cur;
        if (max > 0)
            st.max = max;
        appendDdcLog(QStringLiteral("getvcp %1 -> %2").arg(code, 2, 16, QLatin1Char('0')).arg(cur));
        m_rpc->broadcast(QStringLiteral("ddc"), ddcSnapshot());
    });

    connect(m_ddc, &DdcClient::vcpWritten, this, [this](quint8 code, quint16 value) {
        VcpState& st = m_vcp[code];
        st.current = value;
        settings::saveVcp(code, value);
        appendDdcLog(QStringLiteral("setvcp %1 %2 -> ok").arg(code, 2, 16, QLatin1Char('0')).arg(value));
        m_rpc->broadcast(QStringLiteral("ddc"), ddcSnapshot());
    });

    connect(m_ddc, &DdcClient::errorOccurred, this, [this](const QString& msg) {
        appendDdcLog(msg);
    });

    connect(m_device, &EdgeDevice::stateChanged, this, [this](const EdgeDevice::State&) {
        m_rpc->broadcast(QStringLiteral("device"), deviceSnapshot());
    });

    connect(m_touch, &TouchControl::stateChanged, this, [this](TouchControl::State, const QString&) {
        m_rpc->broadcast(QStringLiteral("touch"), touchSnapshot());
    });

    connect(m_sensors, &SensorSource::updated, this, [this](const SensorSnapshot& s) {
        m_snap = s;
        if (m_sensorStreaming)
            m_rpc->broadcast(QStringLiteral("sensors"), sensorSnapshot());
    });

    connect(m_updates, &UpdateChecker::updateAvailable, this,
            [this](const QString& version, const QString& url) {
                m_rpc->broadcast(QStringLiteral("update"),
                                 QJsonObject{ { QStringLiteral("state"), QStringLiteral("available") },
                                              { QStringLiteral("version"), version },
                                              { QStringLiteral("url"), url } });
            });
    connect(m_updates, &UpdateChecker::upToDate, this, [this](const QString& version) {
        m_rpc->broadcast(QStringLiteral("update"),
                         QJsonObject{ { QStringLiteral("state"), QStringLiteral("current") },
                                      { QStringLiteral("version"), version } });
    });
    connect(m_updates, &UpdateChecker::checkFailed, this, [this](const QString& err) {
        m_rpc->broadcast(QStringLiteral("update"),
                         QJsonObject{ { QStringLiteral("state"), QStringLiteral("failed") },
                                      { QStringLiteral("error"), err } });
    });
}

void Api::appendDdcLog(const QString& line)
{
    m_ddcLog.append(line);
    while (m_ddcLog.size() > kDdcLogLines)
        m_ddcLog.removeFirst();
    m_rpc->broadcast(QStringLiteral("ddcLog"), QJsonObject{ { QStringLiteral("line"), line } });
}

QJsonObject Api::ddcSnapshot() const
{
    QJsonObject values;
    for (auto it = m_vcp.constBegin(); it != m_vcp.constEnd(); ++it) {
        values.insert(QStringLiteral("%1").arg(it.key(), 2, 16, QLatin1Char('0')),
                      QJsonObject{ { QStringLiteral("value"), it->current },
                                   { QStringLiteral("max"), it->max } });
    }

    QJsonArray features;
    for (const VcpFeature& f : m_caps.features) {
        QJsonArray vals;
        for (const VcpValue& v : f.values)
            vals.append(QJsonObject{ { QStringLiteral("code"), int(v.code) },
                                     { QStringLiteral("label"), QString::fromStdString(v.label) } });
        features.append(QJsonObject{
            { QStringLiteral("code"), int(f.code) },
            { QStringLiteral("name"), QString::fromStdString(f.name) },
            { QStringLiteral("continuous"), f.continuous() },
            { QStringLiteral("values"), vals } });
    }

    QJsonArray log;
    for (const QString& l : m_ddcLog)
        log.append(l);

    return QJsonObject{ { QStringLiteral("ready"), m_ddcReady },
                        { QStringLiteral("message"), m_ddcMessage },
                        { QStringLiteral("model"), QString::fromStdString(m_caps.model) },
                        { QStringLiteral("mccs"), QString::fromStdString(m_caps.mccs) },
                        { QStringLiteral("values"), values },
                        { QStringLiteral("features"), features },
                        { QStringLiteral("log"), log } };
}

QJsonObject Api::touchSnapshot() const
{
    QJsonArray ids;
    for (int id : TouchControl::touchDeviceIds())
        ids.append(id);
    static const char* kModeNames[] = { "off", "main-cursor", "own-pointer", "ripple" };
    const int mode = int(TouchControl::mode());
    return QJsonObject{
        { QStringLiteral("mode"), QLatin1String(kModeNames[mode < 0 || mode > 3 ? 0 : mode]) },
        { QStringLiteral("modeIndex"), mode },
        { QStringLiteral("state"), int(m_touch->state()) },
        { QStringLiteral("detail"), m_touch->detail() },
        { QStringLiteral("deviceIds"), ids },
        { QStringLiteral("matrix"), toArray(TouchControl::matrix()) },
        { QStringLiteral("streaming"), m_touchStreaming }
    };
}

QJsonObject Api::deviceSnapshot() const
{
    const EdgeDevice::State s = m_device->state();
    return QJsonObject{ { QStringLiteral("present"), s.present },
                        { QStringLiteral("accessible"), s.accessible },
                        { QStringLiteral("path"), s.path },
                        { QStringLiteral("product"), s.product },
                        { QStringLiteral("serial"), s.serial } };
}

QJsonObject Api::sensorSnapshot() const
{
    return QJsonObject{ { QStringLiteral("cpuLoadPct"), m_snap.cpuLoadPct },
                        { QStringLiteral("cpuTempC"), m_snap.cpuTempC },
                        { QStringLiteral("ramUsedGiB"), m_snap.ramUsedGiB },
                        { QStringLiteral("ramTotalGiB"), m_snap.ramTotalGiB },
                        { QStringLiteral("ramPct"), m_snap.ramPct },
                        { QStringLiteral("gpuUtilPct"), m_snap.gpuUtilPct },
                        { QStringLiteral("gpuTempC"), m_snap.gpuTempC },
                        { QStringLiteral("gpuMemUsedGiB"), m_snap.gpuMemUsedGiB },
                        { QStringLiteral("gpuMemTotalGiB"), m_snap.gpuMemTotalGiB },
                        { QStringLiteral("gpuName"), m_snap.gpuName },
                        { QStringLiteral("gpuOk"), m_snap.gpuOk } };
}

QJsonObject Api::systemSnapshot() const
{
    return QJsonObject{
        { QStringLiteral("version"), QStringLiteral(EDGELINE_VERSION) },
        { QStringLiteral("ddcutil"), toolVersion(QStringLiteral("ddcutil"), { QStringLiteral("--version") }) },
        { QStringLiteral("xinput"), toolVersion(QStringLiteral("xinput"), { QStringLiteral("--version") }) },
        { QStringLiteral("sessionType"), qEnvironmentVariable("XDG_SESSION_TYPE") },
        { QStringLiteral("socket"), m_rpc->socketPath() }
    };
}

void Api::setTouchStreaming(bool on)
{
    if (on == m_touchStreaming)
        return;
    if (on) {
        if (!m_touchStream)
            m_touchStream = new TouchEventSource(this);
        connect(m_touchStream, &TouchEventSource::touch, this,
                [this](int id, TouchEventSource::Phase phase, double nx, double ny) {
                    static const char* kPhase[] = { "begin", "update", "end" };
                    m_rpc->broadcast(QStringLiteral("touch.point"),
                                     QJsonObject{ { QStringLiteral("id"), id },
                                                  { QStringLiteral("phase"), QLatin1String(kPhase[int(phase)]) },
                                                  { QStringLiteral("nx"), nx },
                                                  { QStringLiteral("ny"), ny } });
                }, Qt::UniqueConnection);
        m_touchStreaming = m_touchStream->start();
    } else {
        if (m_touchStream)
            m_touchStream->stop();
        m_touchStreaming = false;
    }
}

void Api::registerMethods()
{
    m_rpc->addMethod(QStringLiteral("system.info"), [this](const QJsonObject&, QJsonObject& r, QString&) {
        r = systemSnapshot();
        return true;
    });

    // One call the UI makes on connect, so a reconnect repaints correctly
    // without a burst of round trips.
    m_rpc->addMethod(QStringLiteral("state.all"), [this](const QJsonObject&, QJsonObject& r, QString&) {
        r = QJsonObject{ { QStringLiteral("system"), systemSnapshot() },
                         { QStringLiteral("device"), deviceSnapshot() },
                         { QStringLiteral("ddc"), ddcSnapshot() },
                         { QStringLiteral("touch"), touchSnapshot() },
                         { QStringLiteral("sensors"), sensorSnapshot() } };
        return true;
    });

    m_rpc->addMethod(QStringLiteral("device.state"), [this](const QJsonObject&, QJsonObject& r, QString&) {
        r = deviceSnapshot();
        return true;
    });

    m_rpc->addMethod(QStringLiteral("ddc.state"), [this](const QJsonObject&, QJsonObject& r, QString&) {
        r = ddcSnapshot();
        return true;
    });

    m_rpc->addMethod(QStringLiteral("ddc.get"), [this](const QJsonObject& p, QJsonObject& r, QString& e) {
        if (!m_ddcReady) { e = QStringLiteral("no panel on DDC yet"); return false; }
        const int code = p.value(QStringLiteral("code")).toInt(-1);
        if (code < 0 || code > 255) { e = QStringLiteral("'code' must be 0..255"); return false; }
        m_ddc->getVcp(quint8(code));
        r.insert(QStringLiteral("queued"), true);
        return true;
    });

    m_rpc->addMethod(QStringLiteral("ddc.set"), [this](const QJsonObject& p, QJsonObject& r, QString& e) {
        if (!m_ddcReady) { e = QStringLiteral("no panel on DDC yet"); return false; }
        const int code = p.value(QStringLiteral("code")).toInt(-1);
        const int value = p.value(QStringLiteral("value")).toInt(-1);
        if (code < 0 || code > 255) { e = QStringLiteral("'code' must be 0..255"); return false; }
        if (value < 0 || value > 65535) { e = QStringLiteral("'value' out of range"); return false; }
        // Refuse a write the panel never advertised rather than letting ddcutil
        // fail slowly for every slider drag.
        if (!m_caps.features.empty() && !m_caps.has(uint8_t(code))) {
            e = QStringLiteral("this panel does not advertise VCP 0x%1")
                    .arg(code, 2, 16, QLatin1Char('0'));
            return false;
        }
        m_ddc->setVcp(quint8(code), quint16(value));
        r.insert(QStringLiteral("queued"), true);
        return true;
    });

    m_rpc->addMethod(QStringLiteral("ddc.restoreDefaults"),
                     [this](const QJsonObject& p, QJsonObject& r, QString& e) {
        if (!m_ddcReady) { e = QStringLiteral("no panel on DDC yet"); return false; }
        const QString scope = p.value(QStringLiteral("scope")).toString(QStringLiteral("factory"));
        int code = kRestoreFactory;
        if (scope == QLatin1String("brightness")) code = kRestoreBrightness;
        else if (scope == QLatin1String("color")) code = kRestoreColor;
        else if (scope != QLatin1String("factory")) {
            e = QStringLiteral("scope must be factory, brightness or color");
            return false;
        }
        if (!m_caps.features.empty() && !m_caps.has(uint8_t(code))) {
            e = QStringLiteral("this panel cannot restore that group");
            return false;
        }
        m_ddc->setVcp(quint8(code), 1);
        // The panel moves several values at once, so re-read rather than
        // guessing what it landed on.
        for (int c : { 0x10, 0x12, 0x14, 0x16, 0x18, 0x1A, 0x87 })
            m_ddc->getVcp(quint8(c));
        r.insert(QStringLiteral("scope"), scope);
        return true;
    });

    m_rpc->addMethod(QStringLiteral("touch.state"), [this](const QJsonObject&, QJsonObject& r, QString&) {
        r = touchSnapshot();
        return true;
    });

    m_rpc->addMethod(QStringLiteral("touch.setMode"), [this](const QJsonObject& p, QJsonObject& r, QString& e) {
        const QString name = p.value(QStringLiteral("mode")).toString();
        int idx = -1;
        if (name == QLatin1String("off")) idx = 0;
        else if (name == QLatin1String("main-cursor")) idx = 1;
        else if (name == QLatin1String("own-pointer")) idx = 2;
        else if (name == QLatin1String("ripple")) idx = 3;
        else { e = QStringLiteral("mode must be off, main-cursor, own-pointer or ripple"); return false; }

        if (!m_touch->setMode(TouchControl::Mode(idx))) {
            e = QStringLiteral("could not apply the mode (xinput failed)");
            return false;
        }
        settings::saveTouchMode(idx);
        // Ripple mode floats the digitizer, which is the only mode where the
        // agent sees touches before any pointer does.
        setTouchStreaming(idx == 3);
        r = touchSnapshot();
        return true;
    });

    m_rpc->addMethod(QStringLiteral("touch.probe"), [](const QJsonObject&, QJsonObject& r, QString&) {
        const TouchReport rep = TouchProbe::run();
        QJsonArray ifaces;
        for (const auto& i : rep.interfaces)
            ifaces.append(QJsonObject{
                { QStringLiteral("hidId"), QString::fromStdString(i.hidId) },
                { QStringLiteral("driver"), QString::fromStdString(i.driver) },
                { QStringLiteral("inputName"), QString::fromStdString(i.inputName) },
                { QStringLiteral("eventNode"), QString::fromStdString(i.eventNode) },
                { QStringLiteral("descriptorBytes"), int(i.descriptorBytes) },
                { QStringLiteral("fingerCollections"), i.fingerCollections },
                { QStringLiteral("contactCountMax"), i.contactCountMax },
                { QStringLiteral("configReportId"), i.configReportId } });
        QJsonArray xdevs;
        for (const auto& x : rep.xdevices)
            xdevs.append(QJsonObject{
                { QStringLiteral("id"), x.id },
                { QStringLiteral("name"), QString::fromStdString(x.name) },
                { QStringLiteral("eventNode"), QString::fromStdString(x.eventNode) },
                { QStringLiteral("hasTouchClass"), x.hasTouchClass },
                { QStringLiteral("maxContacts"), x.maxContacts },
                { QStringLiteral("directMode"), x.directMode } });
        r = QJsonObject{ { QStringLiteral("haveDigitizer"), rep.haveDigitizer },
                         { QStringLiteral("usbId"), QString::fromStdString(rep.usbId) },
                         { QStringLiteral("interfaces"), ifaces },
                         { QStringLiteral("xdevices"), xdevs },
                         { QStringLiteral("verdict"), QString::fromStdString(rep.verdict) },
                         { QStringLiteral("note"), QString::fromStdString(rep.note) } };
        return true;
    });

    m_rpc->addMethod(QStringLiteral("touch.stream"), [this](const QJsonObject& p, QJsonObject& r, QString& e) {
        const bool on = p.value(QStringLiteral("enabled")).toBool(true);
        setTouchStreaming(on);
        if (on && !m_touchStreaming) {
            e = m_touchStream ? m_touchStream->lastError() : QStringLiteral("could not start the touch reader");
            return false;
        }
        r.insert(QStringLiteral("streaming"), m_touchStreaming);
        return true;
    });

    m_rpc->addMethod(QStringLiteral("touch.applyOutputMapping"),
                     [](const QJsonObject&, QJsonObject& r, QString& e) {
        if (!TouchControl::applyOutputMapping()) {
            e = QStringLiteral("could not map touch to the Edge output");
            return false;
        }
        r.insert(QStringLiteral("matrix"), toArray(TouchControl::matrix()));
        return true;
    });

    m_rpc->addMethod(QStringLiteral("touch.setMatrix"), [](const QJsonObject& p, QJsonObject& r, QString& e) {
        const QJsonArray a = p.value(QStringLiteral("matrix")).toArray();
        if (a.size() != 9) { e = QStringLiteral("'matrix' must have 9 numbers"); return false; }
        QList<double> m;
        for (const QJsonValue& v : a)
            m.append(v.toDouble());
        if (!TouchControl::setMatrix(m)) { e = QStringLiteral("xinput refused the matrix"); return false; }
        r.insert(QStringLiteral("matrix"), toArray(TouchControl::matrix()));
        return true;
    });

    // Solve, and only write when asked. The design shows the residual before
    // anything is applied, so a bad run can be redone rather than lived with.
    m_rpc->addMethod(QStringLiteral("touch.calibrate"), [](const QJsonObject& p, QJsonObject& r, QString& e) {
        CalibrationInput in;
        in.targets = pointsFrom(p.value(QStringLiteral("targets")).toArray());
        in.measured = pointsFrom(p.value(QStringLiteral("measured")).toArray());
        in.virtualWidth = p.value(QStringLiteral("virtualWidth")).toDouble();
        in.virtualHeight = p.value(QStringLiteral("virtualHeight")).toDouble();
        in.originX = p.value(QStringLiteral("originX")).toDouble();
        in.originY = p.value(QStringLiteral("originY")).toDouble();

        const QJsonArray base = p.value(QStringLiteral("base")).toArray();
        if (base.size() == 9)
            for (int i = 0; i < 9; ++i)
                in.base[size_t(i)] = base.at(i).toDouble();
        else {
            const QList<double> live = TouchControl::matrix();
            for (int i = 0; i < 9 && i < live.size(); ++i)
                in.base[size_t(i)] = live.at(i);
        }

        const CalibrationResult res = solveCalibration(in);
        if (!res.ok) { e = QString::fromStdString(res.error); return false; }

        QJsonArray mat;
        for (double d : res.matrix)
            mat.append(d);
        QJsonArray resid;
        for (double d : res.residualsPx)
            resid.append(d);

        bool applied = false;
        if (p.value(QStringLiteral("apply")).toBool(false)) {
            QList<double> m;
            for (double d : res.matrix)
                m.append(d);
            applied = TouchControl::setMatrix(m);
            if (!applied) { e = QStringLiteral("solved, but xinput refused the matrix"); return false; }
        }
        r = QJsonObject{ { QStringLiteral("matrix"), mat },
                         { QStringLiteral("residualsPx"), resid },
                         { QStringLiteral("rmsPx"), res.rmsPx },
                         { QStringLiteral("worstPx"), res.worstPx },
                         { QStringLiteral("applied"), applied } };
        return true;
    });

    m_rpc->addMethod(QStringLiteral("sensors.stream"), [this](const QJsonObject& p, QJsonObject& r, QString&) {
        const bool on = p.value(QStringLiteral("enabled")).toBool(true);
        m_sensorStreaming = on;
        if (on)
            m_sensors->start(p.value(QStringLiteral("intervalMs")).toInt(1000));
        else
            m_sensors->stop();
        r.insert(QStringLiteral("streaming"), on);
        return true;
    });

    m_rpc->addMethod(QStringLiteral("updates.check"), [this](const QJsonObject& p, QJsonObject& r, QString&) {
        m_updates->check(p.value(QStringLiteral("manual")).toBool(true));
        r.insert(QStringLiteral("started"), true);
        return true;
    });

    m_rpc->addMethod(QStringLiteral("updates.settings"), [](const QJsonObject& p, QJsonObject& r, QString&) {
        if (p.contains(QStringLiteral("enabled")))
            UpdateChecker::setEnabled(p.value(QStringLiteral("enabled")).toBool());
        if (p.contains(QStringLiteral("skipVersion")))
            UpdateChecker::setSkippedVersion(p.value(QStringLiteral("skipVersion")).toString());
        r = QJsonObject{ { QStringLiteral("enabled"), UpdateChecker::enabled() },
                         { QStringLiteral("skippedVersion"), UpdateChecker::skippedVersion() } };
        return true;
    });
}

} // namespace xen
