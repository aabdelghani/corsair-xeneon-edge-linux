// edgeline-agent: the headless process that owns the panel.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// There is no window here. The agent talks DDC, xinput, XI2 and hidraw, and
// exposes all of it on a UNIX socket. The Electron UI is one client; the CLI
// is another; a shell with socat is a third, which is how this gets debugged.
#include "agent/Api.h"
#include "core/AppSettings.h"
#include "ipc/RpcServer.h"

#include <QCoreApplication>
#include <QTimer>

#include <csignal>
#include <cstdio>
#include <cstring>

namespace {

void printUsage(std::FILE* out, const char* argv0)
{
    std::fprintf(out,
        "usage: %s [--socket PATH] [--version] [--help]\n"
        "\n"
        "Runs headless and owns the Corsair Xeneon Edge: DDC picture control,\n"
        "xinput touch modes, raw touch, telemetry and HID.\n"
        "\n"
        "  --socket PATH   listen here instead of $XDG_RUNTIME_DIR/edgeline.sock\n"
        "  --version       print the version and exit\n"
        "  --help          print this message and exit\n",
        argv0);
}

// Handled before QCoreApplication so they work with no session at all.
int handleEarlyArgs(int argc, char** argv, QString& socketPath)
{
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--version") == 0 || std::strcmp(a, "-V") == 0) {
            std::printf("edgeline-agent %s\n", EDGELINE_VERSION);
            return 0;
        }
        if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
            printUsage(stdout, argv[0]);
            return 0;
        }
        if (std::strcmp(a, "--socket") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s: --socket needs a path\n", argv[0]);
                return 2;
            }
            socketPath = QString::fromLocal8Bit(argv[++i]);
            continue;
        }
        std::fprintf(stderr, "%s: unknown option '%s'\n\n", argv[0], a);
        printUsage(stderr, argv[0]);
        return 2;
    }
    return -1;
}

} // namespace

int main(int argc, char** argv)
{
    QString socketPath;
    const int early = handleEarlyArgs(argc, argv, socketPath);
    if (early >= 0)
        return early;

    QCoreApplication app(argc, argv);
    xen::settings::init();

    xen::RpcServer rpc;
    if (!rpc.listen(socketPath)) {
        std::fprintf(stderr, "edgeline-agent: %s\n", rpc.lastError().toUtf8().constData());
        return 1;
    }

    xen::Api api(&rpc);
    api.start();

    // Leave the socket tidy on a normal shutdown. A SIGKILL still leaves a
    // stale node behind, which is why listen() also probes and clears one.
    static xen::RpcServer* shutdownTarget = &rpc;
    auto onSignal = [](int) {
        if (shutdownTarget)
            shutdownTarget->stop();
        QCoreApplication::quit();
    };
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    std::printf("edgeline-agent %s listening on %s\n",
                EDGELINE_VERSION, rpc.socketPath().toUtf8().constData());
    std::fflush(stdout);

    return QCoreApplication::exec();
}
