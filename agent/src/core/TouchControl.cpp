// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/TouchControl.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>

#include <algorithm>

namespace xen {

namespace {

constexpr const char* kTouchName = "wch.cn TouchScreen";

QString runCapture(const QString& prog, const QStringList& args, int* exitCode = nullptr)
{
    QProcess p;
    p.start(prog, args);
    if (!p.waitForStarted(1500)) {
        if (exitCode)
            *exitCode = -1;
        return {};
    }
    p.waitForFinished(3000);
    if (exitCode)
        *exitCode = p.exitCode();
    return QString::fromUtf8(p.readAllStandardOutput());
}

} // namespace

TouchControl::TouchControl(QObject* parent)
    : QObject(parent)
{
}

bool TouchControl::isX11() 
{
    const auto env = QProcessEnvironment::systemEnvironment();
    const QString session = env.value(QStringLiteral("XDG_SESSION_TYPE"));
    return session.compare(QStringLiteral("x11"), Qt::CaseInsensitive) == 0
        || (!env.value(QStringLiteral("DISPLAY")).isEmpty()
            && session.compare(QStringLiteral("wayland"), Qt::CaseInsensitive) != 0);
}

QList<int> TouchControl::deviceIds()
{
    QList<int> ids;
    int rc = 0;
    const QString out = runCapture(QStringLiteral("xinput"),
                                   { QStringLiteral("list"), QStringLiteral("--short") }, &rc);
    if (rc != 0)
        return ids;
    // Lines like: "⎜   ↳ wch.cn TouchScreen  id=20  [slave  pointer  (2)]" or,
    // when detached from the master, "... id=20 [floating slave]".
    const QStringList lines = out.split('\n');
    static const QRegularExpression re(QStringLiteral("id=(\\d+)"));
    for (const QString& line : lines) {
        if (line.contains(QLatin1String(kTouchName))) {
            const auto m = re.match(line);
            if (m.hasMatch())
                ids.append(m.captured(1).toInt());
        }
    }
    return ids;
}

bool TouchControl::deviceMovesPointer(int id)
{
    // Moves the pointer only if it is enabled AND attached to a master pointer.
    int rc = 0;
    const QString props =
        runCapture(QStringLiteral("xinput"),
                   { QStringLiteral("list-props"), QString::number(id) }, &rc);
    static const QRegularExpression re(QStringLiteral("Device Enabled[^:]*:\\s*(\\d)"));
    const auto m = re.match(props);
    const bool enabled = m.hasMatch() && m.captured(1) == QLatin1String("1");

    // "floating slave" in the short listing => detached, cannot move pointer.
    int rc2 = 0;
    const QString shortList =
        runCapture(QStringLiteral("xinput"), { QStringLiteral("list"), QStringLiteral("--short") },
                   &rc2);
    bool floating = false;
    for (const QString& line : shortList.split('\n')) {
        if (line.contains(QString::asprintf("id=%d", id))
            && line.contains(QLatin1String(kTouchName)))
            floating = line.contains(QStringLiteral("floating"));
    }
    return enabled && !floating;
}

namespace {
constexpr const char* kEdgeMasterName = "xeneon-edge";
}

int TouchControl::deviceMasterId(int id)
{
    // "... id=20 [slave  pointer  (2)]" -> master id 2; "floating" -> -1.
    int rc = 0;
    const QString out =
        runCapture(QStringLiteral("xinput"), { QStringLiteral("list"), QStringLiteral("--short") },
                   &rc);
    for (const QString& line : out.split('\n')) {
        if (line.contains(QString::asprintf("id=%d", id))
            && line.contains(QLatin1String(kTouchName))) {
            if (line.contains(QStringLiteral("floating")))
                return -1;
            // "[slave  pointer  (2)]" -> master id 2 (note trailing ']').
            static const QRegularExpression re(QStringLiteral("pointer\\s*\\((\\d+)\\)"));
            const auto m = re.match(line);
            if (m.hasMatch())
                return m.captured(1).toInt();
        }
    }
    return -1;
}

int TouchControl::ensureEdgeMaster()
{
    auto findMaster = []() -> int {
        int rc = 0;
        const QString out = runCapture(
            QStringLiteral("xinput"), { QStringLiteral("list"), QStringLiteral("--short") }, &rc);
        static const QRegularExpression idre(QStringLiteral("id=(\\d+)"));
        for (const QString& line : out.split('\n')) {
            if (line.contains(QLatin1String(kEdgeMasterName))
                && line.contains(QStringLiteral("master pointer"))) {
                const auto m = idre.match(line);
                if (m.hasMatch())
                    return m.captured(1).toInt();
            }
        }
        return -1;
    };
    int const id = findMaster();
    if (id > 0)
        return id;
    int rc = 0;
    runCapture(QStringLiteral("xinput"),
               { QStringLiteral("create-master"), QLatin1String(kEdgeMasterName) }, &rc);
    return findMaster();
}

TouchControl::Mode TouchControl::mode()
{
    const QList<int> ids = deviceIds();
    if (ids.isEmpty())
        return Mode::Off;

    // Enabled? (any disabled -> Off)
    for (int const id : ids) {
        int rc = 0;
        const QString props = runCapture(
            QStringLiteral("xinput"), { QStringLiteral("list-props"), QString::number(id) }, &rc);
        static const QRegularExpression re(QStringLiteral("Device Enabled[^:]*:\\s*(\\d)"));
        const auto m = re.match(props);
        if (!(m.hasMatch() && m.captured(1) == QLatin1String("1")))
            return Mode::Off;
    }

    const int core = masterPointerId();
    bool anyFloating = false;
    bool anyNonCore = false;
    for (int const id : ids) {
        const int master = deviceMasterId(id);
        if (master < 0)
            anyFloating = true;
        else if (master != core)
            anyNonCore = true;
    }
    if (anyFloating)
        return Mode::Indicator; // floating: drives no pointer, ripple-only
    if (anyNonCore)
        return Mode::Independent;
    return Mode::MainCursor;
}

bool TouchControl::setMode(Mode m)
{
    if (!isX11()) {
        m_detail = tr("Touch modes need X11 (xinput).");
        emit stateChanged(State::Unsupported, m_detail);
        return false;
    }
    const QList<int> ids = deviceIds();
    if (ids.isEmpty()) {
        refresh();
        return false;
    }

    if (m == Mode::Off) {
        for (int const id : ids)
            runCapture(QStringLiteral("xinput"),
                       { QStringLiteral("disable"), QString::number(id) });
        refresh();
        return true;
    }

    if (m == Mode::Indicator) {
        // Float the devices so they drive NO pointer at all; raw touch events
        // still fire and are read by TouchEventSource for the ripple overlay.
        for (int const id : ids) {
            runCapture(QStringLiteral("xinput"), { QStringLiteral("enable"), QString::number(id) });
            runCapture(QStringLiteral("xinput"), { QStringLiteral("float"), QString::number(id) });
        }
        applyOutputMapping(); // keep the valuator geometry consistent
        removeEdgeMasterIfEmpty();
        refresh();
        return true;
    }

    int master = -1;
    if (m == Mode::MainCursor)
        master = masterPointerId();
    else
        master = ensureEdgeMaster();

    bool const ok = master > 0;
    for (int const id : ids) {
        runCapture(QStringLiteral("xinput"), { QStringLiteral("enable"), QString::number(id) });
        if (master > 0)
            runCapture(QStringLiteral("xinput"),
                       { QStringLiteral("reattach"), QString::number(id),
                         QString::number(master) });
    }
    applyOutputMapping();
    if (m == Mode::MainCursor)
        removeEdgeMasterIfEmpty(); // leaving Independent may orphan its master
    refresh();
    return ok;
}

void TouchControl::removeEdgeMasterIfEmpty()
{
    int rc = 0;
    const QString out = runCapture(
        QStringLiteral("xinput"), { QStringLiteral("list"), QStringLiteral("--short") }, &rc);
    const QStringList lines = out.split('\n');
    int masterId = -1;
    for (const QString& line : lines) {
        if (line.contains(QLatin1String(kEdgeMasterName))
            && line.contains(QStringLiteral("master pointer"))) {
            static const QRegularExpression idre(QStringLiteral("id=(\\d+)"));
            const auto mm = idre.match(line);
            if (mm.hasMatch())
                masterId = mm.captured(1).toInt();
        }
    }
    if (masterId < 0)
        return;
    const QString tag = QString::asprintf("(%d)", masterId);
    const bool stillInUse = std::any_of(lines.begin(), lines.end(), [&tag](const QString& line) {
        return line.contains(QStringLiteral("slave")) && line.contains(tag);
    });
    if (stillInUse)
        return;
    runCapture(QStringLiteral("xinput"),
               { QStringLiteral("remove-master"), QString::number(masterId) });
}

int TouchControl::masterPointerId()
{
    int rc = 0;
    const QString out =
        runCapture(QStringLiteral("xinput"), { QStringLiteral("list"), QStringLiteral("--short") },
                   &rc);
    static const QRegularExpression idre(QStringLiteral("id=(\\d+)"));
    for (const QString& line : out.split('\n')) {
        if (line.contains(QStringLiteral("master pointer"))) {
            const auto m = idre.match(line);
            if (m.hasMatch())
                return m.captured(1).toInt();
        }
    }
    return -1;
}

QString TouchControl::edgeOutputName()
{
    int rc = 0;
    const QString out = runCapture(QStringLiteral("xrandr"), { QStringLiteral("--query") }, &rc);
    if (rc != 0)
        return {};
    // Find the "<OUTPUT> connected ... 2560x720+X+Y ..." line.
    static const QRegularExpression re(
        QStringLiteral("(?m)^(\\S+) connected[^\\n]*\\b2560x720\\+"));
    const auto m = re.match(out);
    return m.hasMatch() ? m.captured(1) : QString();
}

TouchControl::State TouchControl::refresh()
{
    if (!isX11()) {
        m_state = State::Unsupported;
        m_detail = tr("Touch toggle needs X11 (xinput). This session isn't X11.");
        emit stateChanged(m_state, m_detail);
        return m_state;
    }

    const QList<int> ids = deviceIds();
    if (ids.isEmpty()) {
        m_state = State::NotFound;
        m_detail = tr("No \"%1\" device found.").arg(QLatin1String(kTouchName));
        emit stateChanged(m_state, m_detail);
        return m_state;
    }

    bool anyMoves = false;
    for (int const id : ids)
        if (deviceMovesPointer(id))
            anyMoves = true;

    m_state = anyMoves ? State::Enabled : State::Disabled;
    m_detail = anyMoves
        ? tr("Touching the Edge moves the mouse pointer (%1 input device(s)).").arg(int(ids.size()))
        : tr("Touch does not move the pointer (disabled or detached).");
    emit stateChanged(m_state, m_detail);
    return m_state;
}

QList<double> TouchControl::matrix()
{
    QList<double> ident{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    const QList<int> ids = deviceIds();
    if (ids.isEmpty())
        return ident;
    int rc = 0;
    const QString props = runCapture(
        QStringLiteral("xinput"),
        { QStringLiteral("list-props"), QString::number(ids.first()) }, &rc);
    static const QRegularExpression re(
        QStringLiteral("Coordinate Transformation Matrix[^:]*:\\s*([^\\n]+)"));
    const auto m = re.match(props);
    if (!m.hasMatch())
        return ident;
    QList<double> out;
    for (const QString& tok : m.captured(1).split(','))
        out.append(tok.trimmed().toDouble());
    return out.size() == 9 ? out : ident;
}

bool TouchControl::applyOutputMapping()
{
    const QString out = edgeOutputName();
    if (out.isEmpty())
        return false;
    bool ok = true;
    for (int const id : deviceIds()) {
        int rc = 0;
        runCapture(QStringLiteral("xinput"),
                   { QStringLiteral("map-to-output"), QString::number(id), out }, &rc);
        if (rc != 0)
            ok = false;
    }
    return ok;
}

bool TouchControl::setMatrix(const QList<double>& m)
{
    if (m.size() != 9)
        return false;
    QStringList vals;
    for (double const v : m)
        vals << QString::number(v, 'g', 10);
    bool ok = true;
    for (int const id : deviceIds()) {
        int rc = 0;
        QStringList args{ QStringLiteral("set-prop"), QString::number(id),
                          QStringLiteral("Coordinate Transformation Matrix") };
        args += vals;
        runCapture(QStringLiteral("xinput"), args, &rc);
        if (rc != 0)
            ok = false;
    }
    return ok;
}

bool TouchControl::setPointerEnabled(bool enabled)
{
    if (!isX11()) {
        m_detail = tr("Not X11; cannot change touch input.");
        emit stateChanged(State::Unsupported, m_detail);
        return false;
    }
    const QList<int> ids = deviceIds();
    if (ids.isEmpty()) {
        refresh();
        return false;
    }
    bool ok = true;
    for (int const id : ids) {
        int rc = 0;
        if (enabled) {
            // Enable, reattach to the master pointer, and map to the Edge output
            // so touches land on the panel and move the cursor there.
            runCapture(QStringLiteral("xinput"),
                       { QStringLiteral("enable"), QString::number(id) }, &rc);
            if (rc != 0)
                ok = false;
            const int master = masterPointerId();
            if (master > 0)
                runCapture(QStringLiteral("xinput"),
                           { QStringLiteral("reattach"), QString::number(id),
                             QString::number(master) });
            const QString out = edgeOutputName();
            if (!out.isEmpty())
                runCapture(QStringLiteral("xinput"),
                           { QStringLiteral("map-to-output"), QString::number(id), out });
        } else {
            runCapture(QStringLiteral("xinput"),
                       { QStringLiteral("disable"), QString::number(id) }, &rc);
            if (rc != 0)
                ok = false;
        }
    }
    refresh();
    return ok;
}

} // namespace xen
