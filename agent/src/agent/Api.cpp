// SPDX-License-Identifier: GPL-3.0-or-later
#include "agent/Api.h"

#include "core/AppSettings.h"
#include "core/Calibration.h"
#include "core/Profiles.h"
#include "x11/FocusWatcher.h"
#include "x11/TouchEventSource.h"
#include "x11/TouchProbe.h"

#include <QJsonArray>
#include <QDateTime>
#include <QDir>
#include <QProcess>
#include <QRegularExpression>

#include <algorithm>
#include <QSet>

namespace xen {
namespace {

constexpr int kDdcLogLines = 40;

// The panel's own restore-defaults features. Writing 1 triggers them.
// The picture controls a profile stores. Power (0xD6) and input source (0x60)
// are excluded on purpose; see captureProfile().
constexpr int kPictureCodes[] = { 0x10, 0x12, 0x14, 0x16, 0x18, 0x1A, 0x87 };

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
    m_rules = rules::load();
    if (m_rules.enabled)
        setRulesActive(true);
    m_touchCfg = touchcfg::load();

    m_ddc->start();
    m_device->startPolling(2000);
    m_touch->refresh();

    // Restore the saved touch mode. The panel remembers its own picture
    // settings across a power cycle, but xinput state is per-session: without
    // this the digitizer is back to driving the main cursor at every login,
    // which is what the old app's --restore mode existed to prevent.
    const int saved = settings::loadTouchMode(-1);
    if (saved >= 0 && saved <= 3 && int(TouchControl::mode()) != saved) {
        m_touch->setMode(TouchControl::Mode(saved));
    }
    syncTouchStreaming();
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
                        { QStringLiteral("gpuOk"), m_snap.gpuOk },
                        { QStringLiteral("netInterface"), m_snap.netInterface },
                        { QStringLiteral("netRxMBs"), m_snap.netRxMBs },
                        { QStringLiteral("netTxMBs"), m_snap.netTxMBs },
                        { QStringLiteral("diskDevice"), m_snap.diskDevice },
                        { QStringLiteral("diskReadMBs"), m_snap.diskReadMBs },
                        { QStringLiteral("diskWriteMBs"), m_snap.diskWriteMBs },
                        { QStringLiteral("failedUnits"), m_snap.failedUnits },
                        { QStringLiteral("failedUnitNames"),
                          QJsonArray::fromStringList(m_snap.failedUnitNames) } };
}

QString Api::toolPath(const QString& exe)
{
    QProcess p;
    p.start(QStringLiteral("which"), { exe });
    if (!p.waitForStarted(1000) || !p.waitForFinished(2000))
        return {};
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

QJsonObject Api::systemSnapshot() const
{
    // The bus the panel was actually found on, so the Settings page can show
    // it rather than claiming a number nothing verified.
    int bus = -1;
    static const QRegularExpression busRe(QStringLiteral("bus (\\d+)"));
    if (const auto m = busRe.match(m_ddcMessage); m.hasMatch())
        bus = m.captured(1).toInt();

    return QJsonObject{
        { QStringLiteral("version"), QStringLiteral(EDGELINE_VERSION) },
        { QStringLiteral("codename"), QStringLiteral(EDGELINE_CODENAME) },
        { QStringLiteral("configPath"), settings::configPath() },
        { QStringLiteral("profilesPath"), profiles::directory() },
        { QStringLiteral("ddcutilPath"), toolPath(QStringLiteral("ddcutil")) },
        { QStringLiteral("xinputPath"), toolPath(QStringLiteral("xinput")) },
        { QStringLiteral("i2cBus"), bus },
        { QStringLiteral("autostart"), settings::autostartEnabled() },
        { QStringLiteral("updatesEnabled"), UpdateChecker::enabled() },
        { QStringLiteral("hidLog"), QDir::homePath() + QStringLiteral("/.local/share/edgeline/hid.log") },
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
                    onTouchPoint(id, int(phase), nx, ny);
                }, Qt::UniqueConnection);
        m_touchStreaming = m_touchStream->start();
    } else {
        if (m_touchStream)
            m_touchStream->stop();
        m_touchStreaming = false;
    }
}

// A profile is a snapshot of everything the panel is set to right now. Only
// values the panel actually reported are captured: writing back a placeholder
// would be worse than leaving the setting alone.
// The preset that makes RGB gain writable. On this panel it is "User 1"; the
// design called the same idea "Custom". Matched by label because the code
// differs between panels.
bool Api::isUserPreset(int presetCode) const
{
    if (presetCode < 0)
        return false;
    const VcpFeature* f = m_caps.find(0x14);
    if (!f)
        return false;
    for (const VcpValue& v : f->values) {
        if (v.code != presetCode)
            continue;
        std::string label = v.label;
        std::transform(label.begin(), label.end(), label.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        return label.rfind("user", 0) == 0;
    }
    return false;
}

QJsonObject Api::captureProfile() const
{
    QJsonObject vcp;
    for (auto it = m_vcp.constBegin(); it != m_vcp.constEnd(); ++it) {
        if (it->current < 0)
            continue;
        // Power and input source are deliberately not captured. Restoring a
        // profile should not blank the panel because it happened to be blanked
        // when you saved, and should not move the input to a socket you are no
        // longer plugged into, which would black the panel out with no obvious
        // way back. The design's own list of what a profile stores names
        // neither of them.
        if (it.key() == 0xD6 || it.key() == 0x60)
            continue;
        // Gain only means anything under the user preset. Storing it from any
        // other preset records the preset's own numbers and implies they can be
        // restored, which they cannot.
        if ((it.key() == 0x16 || it.key() == 0x18 || it.key() == 0x1A)
            && !isUserPreset(m_vcp.value(0x14).current))
            continue;
        vcp.insert(QStringLiteral("%1").arg(it.key(), 2, 16, QLatin1Char('0')), it->current);
    }
    QJsonArray matrix;
    for (double d : TouchControl::matrix())
        matrix.append(d);

    return QJsonObject{
        { QStringLiteral("vcp"), vcp },
        { QStringLiteral("touchMode"), touchSnapshot().value(QStringLiteral("mode")) },
        { QStringLiteral("matrix"), matrix },
        { QStringLiteral("savedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate) },
    };
}

// Picture values the panel supports but the agent has not read back yet.
// Saving a profile in that window would write a file that silently restores
// only part of your settings, which is worse than refusing.
QStringList Api::missingPictureValues() const
{
    QStringList missing;
    for (int code : kPictureCodes) {
        if (!m_caps.features.empty() && !m_caps.has(uint8_t(code)))
            continue;   // this panel does not have it, so it is not missing
        const auto it = m_vcp.constFind(code);
        if (it == m_vcp.constEnd() || it->current < 0)
            missing << QStringLiteral("0x%1").arg(code, 2, 16, QLatin1Char('0'));
    }
    return missing;
}

// The one-line summary the profile list shows: brightness, preset, touch mode.
QString Api::profileSummary(const QJsonObject& body) const
{
    const QJsonObject vcp = body.value(QStringLiteral("vcp")).toObject();
    QStringList bits;
    if (vcp.contains(QStringLiteral("10")))
        bits << QString::number(vcp.value(QStringLiteral("10")).toInt());
    if (vcp.contains(QStringLiteral("14"))) {
        const int code = vcp.value(QStringLiteral("14")).toInt();
        QString label = QStringLiteral("preset 0x%1").arg(code, 2, 16, QLatin1Char('0'));
        if (const VcpFeature* f = m_caps.find(0x14))
            for (const VcpValue& v : f->values)
                if (v.code == code)
                    label = QString::fromStdString(v.label);
        bits << label;
    }
    const QString mode = body.value(QStringLiteral("touchMode")).toString();
    if (!mode.isEmpty())
        bits << mode;
    return bits.join(QStringLiteral(" · "));
}

QJsonObject Api::rulesSnapshot() const
{
    QJsonObject o = rules::toJson(m_rules);
    o.insert(QStringLiteral("watching"), m_focus && m_focus->running());
    o.insert(QStringLiteral("path"), rules::path());
    o.insert(QStringLiteral("focused"), focusSnapshot());
    if (!m_ruleAppliedProfile.isEmpty())
        o.insert(QStringLiteral("appliedProfile"), m_ruleAppliedProfile);
    return o;
}

QJsonObject Api::focusSnapshot() const
{
    QJsonArray states;
    for (const std::string& s : m_focused.states)
        states.append(QString::fromStdString(s));
    return QJsonObject{
        { QStringLiteral("valid"), m_focused.valid },
        { QStringLiteral("wmInstance"), QString::fromStdString(m_focused.wmInstance) },
        { QStringLiteral("wmClass"), QString::fromStdString(m_focused.wmClass) },
        { QStringLiteral("wmClassFull"),
          QStringLiteral("%1.%2").arg(QString::fromStdString(m_focused.wmInstance),
                                      QString::fromStdString(m_focused.wmClass)) },
        { QStringLiteral("states"), states },
    };
}

std::vector<ColorDevice> Api::readColorDevices()
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(QStringLiteral("colormgr"), { QStringLiteral("get-devices-by-kind"),
                                          QStringLiteral("display") });
    if (!p.waitForStarted(1500) || !p.waitForFinished(6000))
        return {};
    if (p.exitCode() != 0)
        return {};
    return parseColorDevices(QString::fromUtf8(p.readAll()).toStdString());
}

QJsonObject Api::colorSnapshot() const
{
    const std::vector<ColorDevice> devices = readColorDevices();
    const ColorDevice* edge = findEdge(devices);
    if (!edge) {
        return QJsonObject{
            { QStringLiteral("available"), !devices.empty() },
            { QStringLiteral("reason"), devices.empty()
                  ? QStringLiteral("colord is not running, or colormgr is not installed")
                  : QStringLiteral("colord does not know this panel as a display device") },
        };
    }
    QJsonArray profiles;
    for (const IccProfile& pr : edge->profiles)
        profiles.append(QJsonObject{
            { QStringLiteral("id"), QString::fromStdString(pr.id) },
            { QStringLiteral("filename"), QString::fromStdString(pr.filename) },
            { QStringLiteral("autoEdid"), pr.isAutoEdid() },
        });
    const IccProfile* def = edge->defaultProfile();
    return QJsonObject{
        { QStringLiteral("available"), true },
        { QStringLiteral("deviceId"), QString::fromStdString(edge->deviceId) },
        { QStringLiteral("model"), QString::fromStdString(edge->model) },
        { QStringLiteral("output"), QString::fromStdString(edge->xrandrName) },
        { QStringLiteral("profiles"), profiles },
        { QStringLiteral("defaultProfile"),
          def ? QString::fromStdString(def->filename) : QString() },
        // The design locks gain "while a profile is bound". colord binds an
        // automatic EDID profile to every display, so only a real measured
        // profile counts, or gain would be locked on a panel nobody calibrated.
        { QStringLiteral("calibrated"), edge->hasCalibration() },
    };
}

void Api::setRulesActive(bool on)
{
    if (on) {
        if (!m_focus) {
            m_focus = new FocusWatcher(this);
            connect(m_focus, &FocusWatcher::focusChanged, this, &Api::onFocusChanged);
        }
        if (!m_focus->running() && !m_focus->start())
            return;
        m_focused = m_focus->current();
    } else if (m_focus) {
        m_focus->stop();
        m_ruleAppliedProfile.clear();
        m_profileBeforeRule.clear();
    }
}

void Api::onFocusChanged(const WindowInfo& win)
{
    m_focused = win;
    m_rpc->broadcast(QStringLiteral("focus"), focusSnapshot());
    if (!m_rules.enabled)
        return;

    const RuleMatch m = evaluateRules(win, m_rules.rules,
                                      m_rules.fallbackProfile.toStdString());

    QString target = QString::fromStdString(m.profile);
    if (target.isEmpty()) {
        // Nothing matched and no fallback. Put back whatever was active before
        // a rule last took over, if asked to, and otherwise leave it alone.
        if (m_rules.restoreOnUnfocus && !m_ruleAppliedProfile.isEmpty()
            && !m_profileBeforeRule.isEmpty()) {
            target = m_profileBeforeRule;
            m_ruleAppliedProfile.clear();
            m_profileBeforeRule.clear();
        } else {
            return;
        }
    } else if (m.matched && m_ruleAppliedProfile.isEmpty()) {
        m_profileBeforeRule = profiles::active();
    }

    if (target == profiles::active())
        return;   // already there; do not churn the panel
    if (!profiles::exists(target))
        return;

    QJsonObject result;
    QString error;
    // The same path a click takes, so a rule cannot apply a profile differently
    // from the UI.
    applyProfile(target, result, error);
    m_ruleAppliedProfile = m.matched ? target : QString();
    m_rpc->broadcast(QStringLiteral("rules"), rulesSnapshot());
}

bool Api::applyProfile(const QString& name, QJsonObject& result, QString& error)
{
    if (!profiles::exists(name)) {
        error = QStringLiteral("no profile called '%1'").arg(name);
        return false;
    }
    const QJsonObject body = profiles::load(name);

    int written = 0;
    int skipped = 0;
    const QJsonObject vcp = body.value(QStringLiteral("vcp")).toObject();
    if (m_ddcReady) {
        auto cached = [this](int code) {
            const auto it = m_vcp.constFind(code);
            return it == m_vcp.constEnd() ? -1 : it->current;
        };
        auto wanted = [&vcp](int code) {
            const QString key = QStringLiteral("%1").arg(code, 2, 16, QLatin1Char('0'));
            return vcp.contains(key) ? vcp.value(key).toInt() : -1;
        };

        // Selecting a colour preset resets this panel's RGB gain, even when the
        // preset selected is the one already active. Rewriting it unconditionally
        // destroyed the user's gain values on the first profile apply. So the
        // preset is only written when it actually differs.
        const int wantPreset = wanted(0x14);
        if (wantPreset >= 0 && m_caps.has(0x14)) {
            if (wantPreset != cached(0x14)) {
                m_ddc->setVcp(0x14, quint16(wantPreset));
                ++written;
            } else {
                ++skipped;
            }
        }

        for (int code : { 0x10, 0x12, 0x87 }) {
            const int v = wanted(code);
            if (v < 0 || (!m_caps.features.empty() && !m_caps.has(uint8_t(code))))
                continue;
            m_ddc->setVcp(quint8(code), quint16(v));
            ++written;
        }

        // Gain is only writable while the user preset is selected; under any
        // other preset the panel silently ignores the write and keeps the
        // preset's own values. Attempting it anyway would log a success that
        // did not happen.
        const bool userPreset = isUserPreset(wantPreset >= 0 ? wantPreset : cached(0x14));
        for (int code : { 0x16, 0x18, 0x1A }) {
            const int v = wanted(code);
            if (v < 0 || (!m_caps.features.empty() && !m_caps.has(uint8_t(code))))
                continue;
            if (!userPreset) { ++skipped; continue; }
            m_ddc->setVcp(quint8(code), quint16(v));
            ++written;
        }

        // Read back rather than trusting the writes. --noverify is on for speed,
        // so without this the cache would show what we asked for instead of what
        // the panel did.
        for (int code : { 0x10, 0x12, 0x14, 0x16, 0x18, 0x1A, 0x87 })
            if (m_caps.features.empty() || m_caps.has(uint8_t(code)))
                m_ddc->getVcp(quint8(code));
    }

    const QString mode = body.value(QStringLiteral("touchMode")).toString();
    bool touchOk = true;
    if (!mode.isEmpty()) {
        static const QMap<QString, int> kModes{ { QStringLiteral("off"), 0 },
                                                { QStringLiteral("main-cursor"), 1 },
                                                { QStringLiteral("own-pointer"), 2 },
                                                { QStringLiteral("ripple"), 3 } };
        if (kModes.contains(mode)) {
            touchOk = m_touch->setMode(TouchControl::Mode(kModes.value(mode)));
            if (touchOk) {
                settings::saveTouchMode(kModes.value(mode));
                syncTouchStreaming();
            }
        }
    }

    const QJsonArray matrix = body.value(QStringLiteral("matrix")).toArray();
    bool matrixOk = true;
    if (matrix.size() == 9) {
        QList<double> m;
        for (const QJsonValue& v : matrix)
            m.append(v.toDouble());
        matrixOk = TouchControl::setMatrix(m);
    }

    profiles::setActive(name);
    result = QJsonObject{ { QStringLiteral("name"), name },
                          { QStringLiteral("vcpWritten"), written },
                          { QStringLiteral("vcpSkipped"), skipped },
                          { QStringLiteral("ddcReady"), m_ddcReady },
                          { QStringLiteral("touchApplied"), touchOk },
                          { QStringLiteral("matrixApplied"), matrixOk } };
    m_rpc->broadcast(QStringLiteral("profiles"), QJsonObject{ { QStringLiteral("active"), name } });
    return true;
}

void Api::onTouchPoint(int id, int phase, double nx, double ny)
{
    static const char* kPhase[] = { "begin", "update", "end" };
    const bool begin = phase == 0;
    const bool end = phase == 2;

    // Lock zone. Judged on where the contact STARTED: a palm that lands in the
    // reserved band and then slides out is still the palm you asked to ignore.
    // This only holds while the digitizer is floating, which is Ripple mode. In
    // the pointer modes X has already delivered the touch to a pointer by the
    // time we see it and there is nothing left to suppress.
    if (begin && touchcfg::inLockZone(m_touchCfg.lockZone, nx, ny))
        m_lockedContacts.insert(id);
    if (m_lockedContacts.contains(id)) {
        if (end)
            m_lockedContacts.remove(id);
        m_rpc->broadcast(QStringLiteral("touch.point"),
                         QJsonObject{ { QStringLiteral("id"), id },
                                      { QStringLiteral("phase"), QLatin1String(kPhase[phase]) },
                                      { QStringLiteral("nx"), nx },
                                      { QStringLiteral("ny"), ny },
                                      { QStringLiteral("locked"), true } });
        return;
    }

    if (m_touchCfg.gesturesEnabled) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (begin)
            m_gestures.begin(id, nx, ny, now);
        else if (end) {
            if (const Gesture g = m_gestures.end(id, now)) {
                const QString name = QString::fromStdString(g.name());
                const QString action = m_touchCfg.bindings.value(name);
                m_rpc->broadcast(QStringLiteral("gesture"),
                                 QJsonObject{ { QStringLiteral("gesture"), name },
                                              { QStringLiteral("contacts"), g.contacts },
                                              { QStringLiteral("action"), action } });
                if (!action.isEmpty() && action != QLatin1String("none"))
                    runGestureAction(action);
            }
        } else
            m_gestures.update(id, nx, ny, now);
    }

    m_rpc->broadcast(QStringLiteral("touch.point"),
                     QJsonObject{ { QStringLiteral("id"), id },
                                  { QStringLiteral("phase"), QLatin1String(kPhase[phase]) },
                                  { QStringLiteral("nx"), nx },
                                  { QStringLiteral("ny"), ny } });
}

void Api::runGestureAction(const QString& action)
{
    auto stepBrightness = [this](int delta) {
        const auto it = m_vcp.constFind(0x10);
        if (it == m_vcp.constEnd() || it->current < 0 || !m_ddcReady)
            return;
        const int max = it->max > 0 ? it->max : 100;
        const int next = std::clamp(it->current + delta, 0, max);
        if (next != it->current)
            m_ddc->setVcp(0x10, quint16(next));
    };

    if (action == QLatin1String("brightness-up")) { stepBrightness(+10); return; }
    if (action == QLatin1String("brightness-down")) { stepBrightness(-10); return; }

    if (action == QLatin1String("blank-toggle")) {
        const auto it = m_vcp.constFind(0xD6);
        const bool blanked = it != m_vcp.constEnd() && it->current != 1;
        if (m_ddcReady)
            m_ddc->setVcp(0xD6, blanked ? 0x01 : 0x05);
        return;
    }

    if (action == QLatin1String("profile-next") || action == QLatin1String("profile-previous")) {
        const QStringList names = profiles::list();
        if (names.isEmpty())
            return;
        const int cur = int(names.indexOf(profiles::active()));
        const int step = action == QLatin1String("profile-next") ? 1 : -1;
        // Wraps, and starts from the first profile when none is active.
        const int next = cur < 0 ? 0 : ((cur + step) % names.size() + names.size()) % names.size();
        QJsonObject result;
        QString error;
        applyProfile(names.at(next), result, error);
        return;
    }

    // The dashboard lives in the UI process, so these are relayed rather than
    // done here. The agent owns devices, not windows.
    if (action.startsWith(QLatin1String("dashboard")))
        m_rpc->broadcast(QStringLiteral("ui.action"),
                         QJsonObject{ { QStringLiteral("action"), action } });
}

// The raw stream feeds the ripple overlay, gestures and the lock zone. Any of
// them wanting it is reason enough to run it.
void Api::syncTouchStreaming()
{
    const bool ripple = int(TouchControl::mode()) == 3;
    const bool wanted = ripple || m_touchCfg.gesturesEnabled
        || (m_touchCfg.lockZone.enabled && ripple);
    setTouchStreaming(wanted);
    if (!wanted) {
        m_gestures.reset();
        m_lockedContacts.clear();
    }
}

QJsonObject Api::touchConfigSnapshot() const
{
    QJsonObject o = touchcfg::toJson(m_touchCfg);
    QJsonObject acts;
    const auto all = touchcfg::actions();
    for (auto it = all.constBegin(); it != all.constEnd(); ++it)
        acts.insert(it.key(), it.value());
    o.insert(QStringLiteral("availableActions"), acts);
    o.insert(QStringLiteral("streaming"), m_touchStreaming);
    // The lock zone can only drop a touch while the agent owns the device.
    o.insert(QStringLiteral("lockZoneEffective"), int(TouchControl::mode()) == 3);
    o.insert(QStringLiteral("path"), touchcfg::path());
    return o;
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
                         { QStringLiteral("sensors"), sensorSnapshot() },
                         { QStringLiteral("rules"), rulesSnapshot() },
                         { QStringLiteral("color"), colorSnapshot() },
                         { QStringLiteral("touchConfig"), touchConfigSnapshot() } };
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
        syncTouchStreaming();
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

    m_rpc->addMethod(QStringLiteral("settings.set"), [this](const QJsonObject& p, QJsonObject& e_unused, QString& e) {
        Q_UNUSED(p);
        Q_UNUSED(e_unused);
        // Autostart used to be written here, pointing at the agent. That is
        // wrong: the agent is headless, so "start on login" produced no window
        // and no tray icon and read as the app not starting at all. The UI owns
        // the entry now, because only it knows how this build is launched
        // (a packaged executable, or electron plus an app directory).
        e = QStringLiteral("nothing here to set; autostart is owned by the interface");
        return false;
    });

    // ---------------------------------------------------------------- profiles

    m_rpc->addMethod(QStringLiteral("profiles.list"), [this](const QJsonObject&, QJsonObject& r, QString&) {
        QJsonArray arr;
        for (const QString& name : profiles::list()) {
            const QJsonObject body = profiles::load(name);
            arr.append(QJsonObject{ { QStringLiteral("name"), name },
                                    { QStringLiteral("summary"), profileSummary(body) } });
        }
        r = QJsonObject{ { QStringLiteral("profiles"), arr },
                         { QStringLiteral("active"), profiles::active() },
                         { QStringLiteral("directory"), profiles::directory() } };
        return true;
    });

    m_rpc->addMethod(QStringLiteral("profiles.save"), [this](const QJsonObject& p, QJsonObject& r, QString& e) {
        const QString name = p.value(QStringLiteral("name")).toString();
        if (!p.value(QStringLiteral("overwrite")).toBool(false) && profiles::exists(name)) {
            e = QStringLiteral("'%1' already exists").arg(name);
            return false;
        }
        // Refuse a partial snapshot. The reads are queued behind the capability
        // fetch, so for a second or two after the agent starts some values are
        // not known yet, and a profile saved then would restore only some of
        // your settings without ever saying so.
        if (!m_ddcReady) {
            e = QStringLiteral("no panel on DDC, so there are no picture values to save");
            return false;
        }
        if (const QStringList missing = missingPictureValues(); !missing.isEmpty()) {
            e = QStringLiteral("still reading the panel (%1), try again in a moment")
                    .arg(missing.join(QStringLiteral(", ")));
            return false;
        }
        const QJsonObject body = captureProfile();
        if (!profiles::save(name, body, &e))
            return false;
        profiles::setActive(name);
        r.insert(QStringLiteral("name"), name);
        m_rpc->broadcast(QStringLiteral("profiles"), QJsonObject{ { QStringLiteral("changed"), true } });
        return true;
    });

    m_rpc->addMethod(QStringLiteral("profiles.get"), [](const QJsonObject& p, QJsonObject& r, QString& e) {
        const QString name = p.value(QStringLiteral("name")).toString();
        if (!profiles::exists(name)) { e = QStringLiteral("no profile called '%1'").arg(name); return false; }
        r = profiles::load(name);
        return true;
    });

    m_rpc->addMethod(QStringLiteral("profiles.delete"), [this](const QJsonObject& p, QJsonObject& r, QString& e) {
        if (!profiles::remove(p.value(QStringLiteral("name")).toString(), &e))
            return false;
        r.insert(QStringLiteral("deleted"), true);
        m_rpc->broadcast(QStringLiteral("profiles"), QJsonObject{ { QStringLiteral("changed"), true } });
        return true;
    });

    m_rpc->addMethod(QStringLiteral("profiles.rename"), [this](const QJsonObject& p, QJsonObject& r, QString& e) {
        if (!profiles::rename(p.value(QStringLiteral("from")).toString(),
                              p.value(QStringLiteral("to")).toString(), &e))
            return false;
        r.insert(QStringLiteral("renamed"), true);
        m_rpc->broadcast(QStringLiteral("profiles"), QJsonObject{ { QStringLiteral("changed"), true } });
        return true;
    });

    m_rpc->addMethod(QStringLiteral("profiles.apply"), [this](const QJsonObject& p, QJsonObject& r, QString& e) {
        return applyProfile(p.value(QStringLiteral("name")).toString(), r, e);
    });

    // ---------------------------------------------------------- gestures

    m_rpc->addMethod(QStringLiteral("touch.config"), [this](const QJsonObject&, QJsonObject& r, QString&) {
        r = touchConfigSnapshot();
        return true;
    });

    m_rpc->addMethod(QStringLiteral("touch.setConfig"), [this](const QJsonObject& p, QJsonObject& r, QString& e) {
        // Reject an unknown action rather than dropping it on load, which would
        // leave a binding that looks set and does nothing.
        const QJsonObject binds = p.value(QStringLiteral("gestures")).toObject()
                                      .value(QStringLiteral("bindings")).toObject();
        for (auto it = binds.constBegin(); it != binds.constEnd(); ++it) {
            const QString action = it.value().toString();
            if (!touchcfg::isKnownAction(action)) {
                e = QStringLiteral("'%1' is not an action").arg(action);
                return false;
            }
        }
        touchcfg::Config cfg = touchcfg::fromJson(p);
        if (!touchcfg::save(cfg, &e))
            return false;
        m_touchCfg = cfg;
        syncTouchStreaming();
        r = touchConfigSnapshot();
        m_rpc->broadcast(QStringLiteral("touchConfig"), r);
        return true;
    });

    // ---------------------------------------------------------------- rules

    m_rpc->addMethod(QStringLiteral("rules.get"), [this](const QJsonObject&, QJsonObject& r, QString&) {
        r = rulesSnapshot();
        return true;
    });

    m_rpc->addMethod(QStringLiteral("rules.set"), [this](const QJsonObject& p, QJsonObject& r, QString& e) {
        // Validate against what was sent, not against what fromJson kept:
        // fromJson silently drops a rule with no pattern, so a rule the user
        // added and saved would disappear with the save reporting success.
        const QJsonArray sent = p.value(QStringLiteral("rules")).toArray();
        for (int i = 0; i < sent.size(); ++i) {
            const QJsonObject r = sent.at(i).toObject();
            if (r.value(QStringLiteral("pattern")).toString().trimmed().isEmpty()) {
                e = QStringLiteral("rule %1 has nothing to match on").arg(i + 1);
                return false;
            }
            if (r.value(QStringLiteral("profile")).toString().trimmed().isEmpty()) {
                e = QStringLiteral("rule %1 does not say which profile to apply").arg(i + 1);
                return false;
            }
        }

        rules::Config cfg = rules::fromJson(p);
        // Refuse to persist a rule pointing at a profile that is not there:
        // it would look configured and do nothing.
        for (const AppRule& rule : cfg.rules) {
            const QString prof = QString::fromStdString(rule.profile);
            if (!prof.isEmpty() && !profiles::exists(prof)) {
                e = QStringLiteral("no profile called '%1'").arg(prof);
                return false;
            }
        }
        if (!cfg.fallbackProfile.isEmpty() && !profiles::exists(cfg.fallbackProfile)) {
            e = QStringLiteral("no profile called '%1'").arg(cfg.fallbackProfile);
            return false;
        }
        if (!rules::save(cfg, &e))
            return false;
        m_rules = cfg;
        setRulesActive(cfg.enabled);
        if (cfg.enabled && m_focus && m_focus->running())
            onFocusChanged(m_focus->current());
        r = rulesSnapshot();
        m_rpc->broadcast(QStringLiteral("rules"), r);
        return true;
    });

    // ---------------------------------------------------------------- colour

    m_rpc->addMethod(QStringLiteral("color.state"), [this](const QJsonObject&, QJsonObject& r, QString&) {
        r = colorSnapshot();
        return true;
    });

    m_rpc->addMethod(QStringLiteral("color.setProfile"), [this](const QJsonObject& p, QJsonObject& r, QString& e) {
        const QString deviceId = p.value(QStringLiteral("deviceId")).toString();
        const QString profileId = p.value(QStringLiteral("profileId")).toString();
        if (deviceId.isEmpty() || profileId.isEmpty()) {
            e = QStringLiteral("both 'deviceId' and 'profileId' are required");
            return false;
        }
        QProcess proc;
        proc.setProcessChannelMode(QProcess::MergedChannels);
        proc.start(QStringLiteral("colormgr"),
                   { QStringLiteral("device-make-profile-default"), deviceId, profileId });
        if (!proc.waitForStarted(1500) || !proc.waitForFinished(8000)) {
            e = QStringLiteral("colormgr did not respond");
            return false;
        }
        if (proc.exitCode() != 0) {
            e = QString::fromUtf8(proc.readAll()).trimmed();
            if (e.isEmpty())
                e = QStringLiteral("colormgr refused the profile");
            return false;
        }
        r = colorSnapshot();
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
