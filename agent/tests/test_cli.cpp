// SPDX-License-Identifier: GPL-3.0-or-later
// The shell-facing half of the agent: `edgeline call | methods | watch`.
//
// The test hosts a real RpcServer on a socket inside a temp dir, with fake
// handlers that record what reached them, and runs the real CLI binary against
// it with XDG_RUNTIME_DIR pointed at that dir. The server lives in this
// process, so every wait here spins the event loop: blocking in waitForFinished
// would stop the server from answering and the CLI would just time out.
//
// usage: edgeline_cli_tests <path to the edgeline binary>
#include "check.h"
#include "ipc/RpcServer.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimer>

#include <csignal>
#include <functional>

using namespace xen;

namespace {

constexpr int kWaitMs = 15000;

struct CliResult {
    int code = -1;
    bool crashed = false;
    bool timedOut = false;
    QByteArray out;
    QByteArray err;
};

// Spin the event loop until `done()` holds or the time is up. Polling is crude
// but keeps the test free of per-case signal wiring.
bool spinUntil(const std::function<bool()>& done, int timeoutMs = kWaitMs)
{
    QElapsedTimer clock;
    clock.start();
    while (!done()) {
        if (clock.elapsed() > timeoutMs)
            return false;
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return true;
}

QProcessEnvironment envFor(const QString& runtimeDir)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("XDG_RUNTIME_DIR"), runtimeDir);
    return env;
}

// One CLI child plus whatever it has printed so far.
struct Child {
    QProcess proc;
    QByteArray out;
    QByteArray err;
    bool finished = false;

    Child(const QString& exe, const QString& runtimeDir, const QStringList& args)
    {
        proc.setProcessEnvironment(envFor(runtimeDir));
        QObject::connect(&proc, &QProcess::readyReadStandardOutput, &proc,
                         [this]() { out += proc.readAllStandardOutput(); });
        QObject::connect(&proc, &QProcess::readyReadStandardError, &proc,
                         [this]() { err += proc.readAllStandardError(); });
        QObject::connect(&proc, &QProcess::finished, &proc, [this]() {
            out += proc.readAllStandardOutput();
            err += proc.readAllStandardError();
            finished = true;
        });
        proc.start(exe, args);
    }

    [[nodiscard]] bool started() { return proc.waitForStarted(5000); }
    bool waitFinished(int timeoutMs = kWaitMs) { return spinUntil([this]() { return finished; }, timeoutMs); }
    [[nodiscard]] int lineCount() const { return out.count('\n'); }
};

CliResult runCli(const QString& exe, const QString& runtimeDir, const QStringList& args,
                 const QByteArray& stdinData = QByteArray())
{
    Child c(exe, runtimeDir, args);
    CliResult r;
    if (!c.started()) {
        r.err = "could not start " + exe.toUtf8();
        return r;
    }
    if (!stdinData.isEmpty())
        c.proc.write(stdinData);
    c.proc.closeWriteChannel();
    if (!c.waitFinished()) {
        r.timedOut = true;
        c.proc.kill();
        c.proc.waitForFinished(2000);
    }
    r.code = c.proc.exitCode();
    r.crashed = c.proc.exitStatus() != QProcess::NormalExit;
    r.out = c.out;
    r.err = c.err;
    return r;
}

QList<QByteArray> lines(const QByteArray& text)
{
    QList<QByteArray> out;
    for (const QByteArray& l : text.split('\n'))
        if (!l.isEmpty())
            out << l;
    return out;
}

QJsonObject parseObject(const QByteArray& text)
{
    return QJsonDocument::fromJson(text).object();
}

// {"n": <n>}; a named helper because braces with commas break the CHECK macro.
QJsonObject nObj(int n)
{
    return QJsonObject{ { QStringLiteral("n"), n } };
}

// Argument vector from plain literals; keeps the cases below readable.
QStringList av(std::initializer_list<const char*> items)
{
    QStringList out;
    for (const char* s : items)
        out << QString::fromUtf8(s);
    return out;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <edgeline binary>\n", argv[0]);
        return 2;
    }
    const QString exe = QString::fromLocal8Bit(argv[1]);

    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString runtime = dir.path();
    const QString sock = runtime + QStringLiteral("/edgeline.sock");

    QList<QPair<QString, QJsonObject>> calls;
    RpcServer server;
    server.addMethod(QStringLiteral("test.echo"),
                     [&calls](const QJsonObject& p, QJsonObject& r, QString&) {
                         calls.append({ QStringLiteral("test.echo"), p });
                         r = QJsonObject{ { QStringLiteral("echo"), p },
                                          { QStringLiteral("n"), 1 } };
                         return true;
                     });
    server.addMethod(QStringLiteral("test.fail"),
                     [&calls](const QJsonObject& p, QJsonObject&, QString& e) {
                         calls.append({ QStringLiteral("test.fail"), p });
                         e = QStringLiteral("boom: no panel");
                         return false;
                     });
    // Fakes for the convenience commands. `failing` lists methods that answer
    // with an error; `failSetCode` makes ddc.set fail for one VCP code only.
    QStringList failing;
    int failSetCode = -1;
    QString activeProfile = QStringLiteral("work");
    QJsonArray profileNames{ QStringLiteral("work"), QStringLiteral("night") };

    auto fake = [&](const QString& name, const std::function<QJsonObject()>& answer) {
        server.addMethod(name, [&calls, &failing, name, answer](const QJsonObject& p, QJsonObject& r, QString& e) {
            calls.append({ name, p });
            if (failing.contains(name)) {
                e = QStringLiteral("fake: %1 refused").arg(name);
                return false;
            }
            r = answer();
            return true;
        });
    };
    const QJsonObject profileBody{ { QStringLiteral("brightness"), 50 },
                                   { QStringLiteral("nested"), QJsonObject{ { QStringLiteral("a"), 1 } } } };
    fake(QStringLiteral("profiles.list"), [&]() {
        QJsonArray arr;
        for (const QJsonValue& n : profileNames)
            arr.append(QJsonObject{ { QStringLiteral("name"), n }, { QStringLiteral("summary"), QStringLiteral("s") } });
        return QJsonObject{ { QStringLiteral("profiles"), arr }, { QStringLiteral("active"), activeProfile } };
    });
    fake(QStringLiteral("profiles.get"), [&]() { return profileBody; });
    fake(QStringLiteral("profiles.save"), []() { return QJsonObject{ { QStringLiteral("name"), QStringLiteral("x") } }; });
    fake(QStringLiteral("profiles.apply"), []() { return QJsonObject{ { QStringLiteral("applied"), true } }; });
    fake(QStringLiteral("profiles.delete"), []() { return QJsonObject{ { QStringLiteral("deleted"), true } }; });
    fake(QStringLiteral("profiles.rename"), []() { return QJsonObject{ { QStringLiteral("renamed"), true } }; });

    // Panel state: red/green allow 0..255, blue only 0..200, so a swapped
    // channel check cannot pass by accident.
    const auto ddcValue = [](int value, int max) {
        return QJsonObject{ { QStringLiteral("value"), value }, { QStringLiteral("max"), max } };
    };
    const QJsonObject ddcValues{ { QStringLiteral("10"), ddcValue(40, 100) },
                                 { QStringLiteral("16"), ddcValue(127, 255) },
                                 { QStringLiteral("18"), ddcValue(128, 255) },
                                 { QStringLiteral("1a"), ddcValue(129, 200) } };
    fake(QStringLiteral("ddc.state"), [&]() { return QJsonObject{ { QStringLiteral("values"), ddcValues } }; });
    server.addMethod(QStringLiteral("ddc.set"),
                     [&calls, &failSetCode](const QJsonObject& p, QJsonObject& r, QString& e) {
                         calls.append({ QStringLiteral("ddc.set"), p });
                         if (p.value(QStringLiteral("code")).toInt() == failSetCode) {
                             e = QStringLiteral("fake: ddc.set refused");
                             return false;
                         }
                         r = QJsonObject{ { QStringLiteral("queued"), true } };
                         return true;
                     });
    const QJsonObject stateAll{
        { QStringLiteral("system"), QJsonObject{ { QStringLiteral("version"), QStringLiteral("9.9.9") } } },
        { QStringLiteral("device"), QJsonObject{ { QStringLiteral("present"), true },
                                                 { QStringLiteral("accessible"), true } } },
        { QStringLiteral("ddc"), QJsonObject{ { QStringLiteral("message"), QStringLiteral("ok") },
                                              { QStringLiteral("values"), ddcValues } } },
        { QStringLiteral("touch"), QJsonObject{ { QStringLiteral("mode"), QStringLiteral("own-pointer") } } }
    };
    fake(QStringLiteral("state.all"), [&]() { return stateAll; });

    CHECK(server.listen(sock));

    // ---- call: success, indented and compact ---------------------------------
    {
        const QJsonObject expected{ { QStringLiteral("echo"), QJsonObject{ { QStringLiteral("a"), 1 } } },
                                    { QStringLiteral("n"), 1 } };
        const QJsonObject aObj{ { QStringLiteral("a"), 1 } };
        const CliResult r = runCli(exe, runtime, { QStringLiteral("call"), QStringLiteral("test.echo"),
                                                   QStringLiteral("{\"a\":1}") });
        CHECK(!r.timedOut);
        CHECK(r.code == 0);
        CHECK(r.out == QJsonDocument(expected).toJson(QJsonDocument::Indented));
        CHECK(r.out.count('\n') > 1); // really indented, not one line
        CHECK(!calls.isEmpty() && calls.last().first == QLatin1String("test.echo"));
        CHECK(!calls.isEmpty() && calls.last().second == aObj);
    }
    {
        // Params default to {}. The flag works on either side of the method.
        const QJsonObject expected{ { QStringLiteral("echo"), QJsonObject{} },
                                    { QStringLiteral("n"), 1 } };
        for (const QStringList& args : { QStringList{ QStringLiteral("call"), QStringLiteral("--compact"),
                                                      QStringLiteral("test.echo") },
                                         QStringList{ QStringLiteral("call"), QStringLiteral("test.echo"),
                                                      QStringLiteral("--compact") } }) {
            calls.clear();
            const CliResult r = runCli(exe, runtime, args);
            CHECK(r.code == 0);
            CHECK(r.out == QJsonDocument(expected).toJson(QJsonDocument::Compact) + "\n");
            CHECK(lines(r.out).size() == 1);
            CHECK(calls.size() == 1 && calls.last().second.isEmpty());
        }
    }

    // ---- call: params from stdin ---------------------------------------------
    {
        calls.clear();
        const CliResult r = runCli(exe, runtime,
                                   { QStringLiteral("call"), QStringLiteral("--compact"),
                                     QStringLiteral("test.echo"), QStringLiteral("-") },
                                   "{\"x\":[1,2]}\n");
        CHECK(r.code == 0);
        CHECK(calls.size() == 1);
        const QJsonObject want{ { QStringLiteral("x"), QJsonArray{ 1, 2 } } };
        CHECK(!calls.isEmpty() && calls.last().second == want);
        CHECK(parseObject(r.out).value(QStringLiteral("echo")).toObject() == want);
    }

    // ---- call: params that are not an object never reach the agent -----------
    {
        calls.clear();
        for (const QString& bad : { QStringLiteral("[1]"), QStringLiteral("nope"),
                                    QStringLiteral("\"str\""), QStringLiteral("42") }) {
            const CliResult r = runCli(exe, runtime, { QStringLiteral("call"), QStringLiteral("test.echo"), bad });
            CHECK(r.code == 64);
            CHECK(!r.err.isEmpty());
            CHECK(r.out.isEmpty());
        }
        const CliResult viaStdin = runCli(exe, runtime,
                                          { QStringLiteral("call"), QStringLiteral("test.echo"), QStringLiteral("-") },
                                          "[]");
        CHECK(viaStdin.code == 64);
        CHECK(!viaStdin.err.isEmpty());
        const CliResult noMethod = runCli(exe, runtime, { QStringLiteral("call") });
        CHECK(noMethod.code == 64);
        CHECK(calls.isEmpty());
    }

    // ---- call: agent error reply, unknown method, no agent -------------------
    {
        calls.clear();
        const CliResult r = runCli(exe, runtime, { QStringLiteral("call"), QStringLiteral("test.fail") });
        CHECK(r.code == 3);
        CHECK(r.err.contains("boom: no panel"));
        CHECK(r.out.isEmpty());
        CHECK(calls.size() == 1);

        const CliResult unknown = runCli(exe, runtime, { QStringLiteral("call"), QStringLiteral("no.such") });
        CHECK(unknown.code == 3);
        CHECK(unknown.err.contains("unknown method"));
    }
    {
        QTemporaryDir empty;
        CHECK(empty.isValid());
        const CliResult r = runCli(exe, empty.path(), { QStringLiteral("call"), QStringLiteral("test.echo") });
        CHECK(r.code == 3);
        CHECK(!r.err.isEmpty());
        CHECK(r.out.isEmpty());
        const CliResult m = runCli(exe, empty.path(), { QStringLiteral("methods") });
        CHECK(m.code == 3);
        CHECK(!m.err.isEmpty());
        CHECK(m.out.isEmpty());
    }

    // ---- rpc.methods and `methods` -------------------------------------------
    {
        const QStringList want = av({ "ddc.set", "ddc.state", "profiles.apply", "profiles.delete",
                                      "profiles.get", "profiles.list", "profiles.rename",
                                      "profiles.save", "rpc.methods", "state.all", "test.echo",
                                      "test.fail" });

        const CliResult r = runCli(exe, runtime,
                                   { QStringLiteral("call"), QStringLiteral("--compact"), QStringLiteral("rpc.methods") });
        CHECK(r.code == 0);
        QStringList got;
        for (const QJsonValue& v : parseObject(r.out).value(QStringLiteral("methods")).toArray())
            got << v.toString();
        CHECK(got == want); // sorted, built-in included, nothing else registered

        const CliResult m = runCli(exe, runtime, { QStringLiteral("methods") });
        CHECK(m.code == 0);
        QStringList listed;
        for (const QByteArray& l : lines(m.out))
            listed << QString::fromUtf8(l);
        CHECK(listed == want);
    }

    const auto sentMethods = [&calls]() {
        QStringList m;
        for (const auto& c : calls)
            m << c.first;
        return m;
    };
    const auto run = [&](std::initializer_list<const char*> a) { return runCli(exe, runtime, av(a)); };

    // ---- item 8: --help names the new commands -------------------------------
    {
        const CliResult r = run({ "--help" });
        CHECK(r.code == 0);
        for (const char* word : { "call", "methods", "watch", "profile", "gain" }) {
            const QRegularExpression re(QStringLiteral("\\b%1\\b").arg(QLatin1String(word)));
            CHECK(QString::fromUtf8(r.out).contains(re));
        }
    }

    // ---- item 5: profile list ------------------------------------------------
    {
        calls.clear();
        const CliResult r = run({ "profile", "list" });
        CHECK(r.code == 0);
        CHECK(r.out == QByteArray("* work\n  night\n"));
        CHECK(sentMethods() == QStringList{ QStringLiteral("profiles.list") });

        // Edge: no active profile means every line gets the two-space prefix.
        activeProfile.clear();
        const CliResult none = run({ "profile", "list" });
        CHECK(none.code == 0);
        CHECK(none.out == QByteArray("  work\n  night\n"));

        // Edge: no profiles at all is still a success with nothing to print.
        const QJsonArray saved = profileNames;
        profileNames = QJsonArray{};
        const CliResult empty = run({ "profile", "list" });
        CHECK(empty.code == 0);
        CHECK(empty.out.isEmpty());
        profileNames = saved;
        activeProfile = QStringLiteral("work");

        // Edge: only the active one is marked, also when it is not the first.
        activeProfile = QStringLiteral("night");
        const CliResult second = run({ "profile", "list" });
        CHECK(second.out == QByteArray("  work\n* night\n"));
        activeProfile = QStringLiteral("work");
    }

    // ---- item 5: profile show ------------------------------------------------
    {
        calls.clear();
        const CliResult r = run({ "profile", "show", "work" });
        CHECK(r.code == 0);
        CHECK(r.out == QJsonDocument(profileBody).toJson(QJsonDocument::Indented));
        CHECK(sentMethods() == QStringList{ QStringLiteral("profiles.get") });
        const QJsonObject want{ { QStringLiteral("name"), QStringLiteral("work") } };
        CHECK(!calls.isEmpty() && calls.last().second == want);
    }

    // ---- item 5: save (with and without --overwrite), apply, delete, rename --
    {
        calls.clear();
        const CliResult r = run({ "profile", "save", "work" });
        CHECK(r.code == 0);
        CHECK(lines(r.out).size() == 1); // one confirmation line
        CHECK(r.err.isEmpty());
        const QJsonObject want{ { QStringLiteral("name"), QStringLiteral("work") } };
        CHECK(calls.size() == 1 && calls.last().first == QLatin1String("profiles.save"));
        // Exactly {"name"}: an "overwrite":false key would also be a deviation.
        CHECK(!calls.isEmpty() && calls.last().second == want);
        CHECK(!calls.isEmpty() && !calls.last().second.contains(QStringLiteral("overwrite")));
    }
    {
        // Edge: the flag works on either side of the name.
        const QJsonObject want{ { QStringLiteral("name"), QStringLiteral("work") },
                                { QStringLiteral("overwrite"), true } };
        for (const QStringList& args : { av({ "profile", "save", "work", "--overwrite" }),
                                         av({ "profile", "save", "--overwrite", "work" }) }) {
            calls.clear();
            const CliResult r = runCli(exe, runtime, args);
            CHECK(r.code == 0);
            CHECK(lines(r.out).size() == 1);
            CHECK(calls.size() == 1 && calls.last().first == QLatin1String("profiles.save"));
            CHECK(!calls.isEmpty() && calls.last().second == want);
        }
    }
    {
        // Edge: a name with a space and non-ASCII letters reaches the agent verbatim.
        calls.clear();
        const CliResult r = run({ "profile", "apply", "B\xC3\xBCro Nacht" });
        CHECK(r.code == 0);
        CHECK(lines(r.out).size() == 1);
        const QJsonObject want{ { QStringLiteral("name"), QString::fromUtf8("B\xC3\xBCro Nacht") } };
        CHECK(calls.size() == 1 && calls.last().first == QLatin1String("profiles.apply"));
        CHECK(!calls.isEmpty() && calls.last().second == want);
    }
    {
        calls.clear();
        const CliResult r = run({ "profile", "delete", "night" });
        CHECK(r.code == 0);
        CHECK(lines(r.out).size() == 1);
        const QJsonObject want{ { QStringLiteral("name"), QStringLiteral("night") } };
        CHECK(calls.size() == 1 && calls.last().first == QLatin1String("profiles.delete"));
        CHECK(!calls.isEmpty() && calls.last().second == want);
    }
    {
        calls.clear();
        const CliResult r = run({ "profile", "rename", "work", "office" });
        CHECK(r.code == 0);
        CHECK(lines(r.out).size() == 1);
        const QJsonObject want{ { QStringLiteral("from"), QStringLiteral("work") },
                                { QStringLiteral("to"), QStringLiteral("office") } };
        CHECK(calls.size() == 1 && calls.last().first == QLatin1String("profiles.rename"));
        CHECK(!calls.isEmpty() && calls.last().second == want);
    }

    // ---- item 5: missing arguments and unknown subcommand: 64, nothing sent --
    {
        calls.clear();
        const QList<QStringList> bad{
            av({ "profile" }),
            av({ "profile", "show" }),
            av({ "profile", "save" }),
            av({ "profile", "save", "--overwrite" }), // edge: flag is not a name
            av({ "profile", "apply" }),
            av({ "profile", "delete" }),
            av({ "profile", "rename" }),
            av({ "profile", "rename", "work" }),
            av({ "profile", "frobnicate" }), // edge: unknown subcommand
        };
        for (const QStringList& args : bad) {
            const CliResult r = runCli(exe, runtime, args);
            CHECK(r.code == 64);
            CHECK(!r.err.isEmpty());
            CHECK(r.out.isEmpty());
        }
        CHECK(calls.isEmpty());
    }

    // ---- item 5: error replies: stderr + exit 3, nothing on stdout -----------
    {
        const QList<QStringList> cmds{
            av({ "profile", "list" }),           av({ "profile", "show", "work" }),
            av({ "profile", "save", "work" }),   av({ "profile", "apply", "work" }),
            av({ "profile", "delete", "work" }), av({ "profile", "rename", "work", "x" }),
        };
        const QStringList methods{ QStringLiteral("profiles.list"),   QStringLiteral("profiles.get"),
                                   QStringLiteral("profiles.save"),   QStringLiteral("profiles.apply"),
                                   QStringLiteral("profiles.delete"), QStringLiteral("profiles.rename") };
        for (int i = 0; i < cmds.size(); ++i) {
            failing = QStringList{ methods[i] };
            const CliResult r = runCli(exe, runtime, cmds[i]);
            CHECK(r.code == 3);
            CHECK(r.err.contains(("fake: " + methods[i] + " refused").toUtf8()));
            CHECK(r.out.isEmpty());
        }
        failing.clear();

        // Edge: no agent at all is the same exit 3 for the convenience commands.
        QTemporaryDir empty;
        CHECK(empty.isValid());
        const CliResult r = runCli(exe, empty.path(), av({ "profile", "list" }));
        CHECK(r.code == 3);
        CHECK(!r.err.isEmpty());
        CHECK(r.out.isEmpty());
    }

    // ---- item 6: gain ----------------------------------------------------------
    {
        calls.clear();
        const CliResult r = run({ "gain", "10", "20", "30" });
        CHECK(r.code == 0);
        // ddc.state exactly once, then the writes in R, G, B order (0x16, 0x18, 0x1A).
        const QStringList wantOrder{ QStringLiteral("ddc.state"), QStringLiteral("ddc.set"),
                                     QStringLiteral("ddc.set"), QStringLiteral("ddc.set") };
        CHECK(sentMethods() == wantOrder);
        if (calls.size() == 4) {
            const QJsonObject r1{ { QStringLiteral("code"), 22 }, { QStringLiteral("value"), 10 } };
            const QJsonObject g1{ { QStringLiteral("code"), 24 }, { QStringLiteral("value"), 20 } };
            const QJsonObject b1{ { QStringLiteral("code"), 26 }, { QStringLiteral("value"), 30 } };
            CHECK(calls[1].second == r1);
            CHECK(calls[2].second == g1);
            CHECK(calls[3].second == b1);
        }
    }
    {
        // Edge: values exactly at the per-channel maximum are accepted (255/255/200),
        // and so is zero.
        calls.clear();
        const CliResult r = run({ "gain", "255", "255", "200" });
        CHECK(r.code == 0);
        CHECK(calls.size() == 4);
        calls.clear();
        const CliResult z = run({ "gain", "0", "0", "0" });
        CHECK(z.code == 0);
        CHECK(calls.size() == 4);
    }
    {
        // Above the maximum of any one channel: 65, nothing written. Blue's max
        // is 200 here (not 255), and the offender is tried in each position.
        const QList<QStringList> over{ av({ "gain", "256", "1", "1" }), av({ "gain", "1", "256", "1" }),
                                       av({ "gain", "1", "1", "201" }) };
        for (const QStringList& args : over) {
            calls.clear();
            const CliResult r = runCli(exe, runtime, args);
            CHECK(r.code == 65);
            CHECK(!r.err.isEmpty());
            CHECK(r.out.isEmpty());
            CHECK(!sentMethods().contains(QStringLiteral("ddc.set")));
            CHECK(sentMethods() == QStringList{ QStringLiteral("ddc.state") });
        }
    }
    {
        // Non-number: 64 and nothing written. Edge: too few / too many arguments.
        // Orchestrator additions before the freeze: too many values and a
        // negative one are refused like `set` refuses them.
        const QList<QStringList> bad{ av({ "gain", "a", "2", "3" }),  av({ "gain", "1", "x", "3" }),
                                      av({ "gain", "1", "2", "3.5z" }), av({ "gain", "1", "2" }),
                                      av({ "gain" }), av({ "gain", "1", "2", "3", "4" }),
                                      av({ "gain", "-1", "2", "3" }) };
        for (const QStringList& args : bad) {
            calls.clear();
            const CliResult r = runCli(exe, runtime, args);
            CHECK(r.code == 64);
            CHECK(!r.err.isEmpty());
            CHECK(r.out.isEmpty());
            CHECK(!sentMethods().contains(QStringLiteral("ddc.set")));
        }
    }
    {
        // An error reply on the second set stops the third.
        calls.clear();
        failSetCode = 24;
        const CliResult r = run({ "gain", "1", "2", "3" });
        CHECK(r.code == 3);
        CHECK(r.err.contains("fake: ddc.set refused"));
        const QStringList wantOrder{ QStringLiteral("ddc.state"), QStringLiteral("ddc.set"),
                                     QStringLiteral("ddc.set") };
        CHECK(sentMethods() == wantOrder);
        // Edge: failing on the very first set sends only that one.
        calls.clear();
        failSetCode = 22;
        const CliResult first = run({ "gain", "1", "2", "3" });
        CHECK(first.code == 3);
        CHECK(sentMethods().count(QStringLiteral("ddc.set")) == 1);
        failSetCode = -1;
    }
    {
        // Orchestrator addition before the freeze: when the maximum cannot be
        // read, gain writes anyway, as `set` does when its range check is
        // unavailable.
        calls.clear();
        failing = QStringList{ QStringLiteral("ddc.state") };
        const CliResult r = run({ "gain", "1", "2", "3" });
        CHECK(r.code == 0);
        CHECK(sentMethods().count(QStringLiteral("ddc.set")) == 3);
        failing.clear();
    }

    // ---- item 7: status --json and get --json ----------------------------------
    {
        calls.clear();
        const CliResult r = run({ "status", "--json" });
        CHECK(r.code == 0);
        CHECK(r.out == QJsonDocument(stateAll).toJson(QJsonDocument::Indented));
        CHECK(sentMethods() == QStringList{ QStringLiteral("state.all") });

        // Edge: the plain form keeps working (regression guard).
        const CliResult plain = run({ "status" });
        CHECK(plain.code == 0);
        CHECK(plain.out.startsWith("edgeline 9.9.9"));
        CHECK(!plain.out.startsWith("{"));
    }
    {
        calls.clear();
        const CliResult r = run({ "get", "red", "--json" });
        CHECK(r.code == 0);
        CHECK(r.out == QJsonDocument(ddcValue(127, 255)).toJson(QJsonDocument::Indented));
        // Edge: the flag may precede the property, and blue reports its own max.
        const CliResult blue = run({ "get", "--json", "blue" });
        CHECK(blue.code == 0);
        CHECK(blue.out == QJsonDocument(ddcValue(129, 200)).toJson(QJsonDocument::Indented));

        // Edge: the plain form still prints the bare number (regression guard).
        const CliResult plain = run({ "get", "red" });
        CHECK(plain.code == 0);
        CHECK(plain.out == QByteArray("127\n"));

        // Edge: --json on a property with no value yet keeps get's exit 1 (existing code).
        const CliResult missing = run({ "get", "sharpness", "--json" });
        CHECK(missing.code == 1);
        CHECK(missing.out.isEmpty());

        // Edge: an agent error is still exit 3 with --json.
        failing = QStringList{ QStringLiteral("state.all"), QStringLiteral("ddc.state") };
        const CliResult st = run({ "status", "--json" });
        CHECK(st.code == 3);
        CHECK(st.out.isEmpty());
        const CliResult gt = run({ "get", "red", "--json" });
        CHECK(gt.code == 3);
        CHECK(gt.out.isEmpty());
        failing.clear();
    }

    // ---- watch: filter, flush through a pipe, SIGINT -------------------------
    {
        calls.clear();
        Child w(exe, runtime, { QStringLiteral("watch"), QStringLiteral("ddc"), QStringLiteral("device") });
        CHECK(w.started());
        CHECK(spinUntil([&]() { return server.clientCount() == 1; }));

        server.broadcast(QStringLiteral("touch"), QJsonObject{ { QStringLiteral("n"), 0 } });
        server.broadcast(QStringLiteral("ddc"), QJsonObject{ { QStringLiteral("n"), 1 } });
        server.broadcast(QStringLiteral("sensors"), QJsonObject{ { QStringLiteral("n"), 0 } });
        server.broadcast(QStringLiteral("device"), QJsonObject{ { QStringLiteral("n"), 2 } });
        server.broadcast(QStringLiteral("ddc"), QJsonObject{ { QStringLiteral("n"), 3 } });

        // Events keep their order on the wire, so once the last wanted one has
        // arrived anything unwanted before it would already be in the output.
        CHECK(spinUntil([&]() { return w.lineCount() >= 3; }));
        const QList<QByteArray> got = lines(w.out);
        CHECK(got.size() == 3);
        if (got.size() == 3) {
            CHECK(parseObject(got[0]).value(QStringLiteral("event")).toString() == QLatin1String("ddc"));
            CHECK(parseObject(got[0]).value(QStringLiteral("data")).toObject() == nObj(1));
            CHECK(parseObject(got[1]).value(QStringLiteral("event")).toString() == QLatin1String("device"));
            CHECK(parseObject(got[2]).value(QStringLiteral("event")).toString() == QLatin1String("ddc"));
            CHECK(parseObject(got[2]).value(QStringLiteral("data")).toObject() == nObj(3));
        }

        // The streaming switches are global in the agent: watch must not touch
        // them, or for that matter send any request at all.
        CHECK(calls.isEmpty());

        w.proc.terminate();
        CHECK(w.waitFinished());
        CHECK(w.proc.exitStatus() == QProcess::NormalExit);
        CHECK(w.proc.exitCode() == 0);
        // Orchestrator addition (second freeze): a signal is a normal end, so
        // nothing may claim on stderr that the agent went away.
        CHECK(w.err.isEmpty());
    }
    {
        // No names means every event; SIGINT ends it cleanly as well.
        Child w(exe, runtime, { QStringLiteral("watch") });
        CHECK(w.started());
        CHECK(spinUntil([&]() { return server.clientCount() == 1; }));
        server.broadcast(QStringLiteral("touch"), QJsonObject{ { QStringLiteral("n"), 1 } });
        server.broadcast(QStringLiteral("sensors"), QJsonObject{ { QStringLiteral("n"), 2 } });
        CHECK(spinUntil([&]() { return w.lineCount() >= 2; }));
        const QList<QByteArray> got = lines(w.out);
        CHECK(got.size() == 2);
        CHECK(w.out.contains("\"touch\"") && w.out.contains("\"sensors\""));

        // Never kill(0, ...): once the child is gone processId() is 0 and that
        // would signal this test's whole process group, ctest included.
        const qint64 pid = w.proc.processId();
        CHECK(!w.finished && pid > 0);
        if (!w.finished && pid > 0)
            ::kill(pid_t(pid), SIGINT);
        CHECK(w.waitFinished());
        CHECK(w.proc.exitStatus() == QProcess::NormalExit);
        CHECK(w.proc.exitCode() == 0);
        CHECK(w.err.isEmpty()); // orchestrator addition (second freeze), as above
    }

    // ---- watch: the agent going away ends it with 3 --------------------------
    {
        Child w(exe, runtime, { QStringLiteral("watch") });
        CHECK(w.started());
        CHECK(spinUntil([&]() { return server.clientCount() == 1; }));
        server.stop();
        CHECK(w.waitFinished());
        CHECK(w.proc.exitStatus() == QProcess::NormalExit);
        CHECK(w.proc.exitCode() == 3);
        CHECK(!w.err.isEmpty());
    }

    // ---- watch: nobody listening ---------------------------------------------
    {
        const CliResult r = runCli(exe, runtime, { QStringLiteral("watch") });
        CHECK(r.code == 3);
        CHECK(!r.err.isEmpty());
    }

    return xen::test::report("cli");
}
