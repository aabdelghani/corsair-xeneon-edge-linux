// xeneon-ctl: checks GitHub Releases for a newer version.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What this does: one unauthenticated HTTPS GET to the GitHub Releases API,
// compares the latest tag against XENEON_VERSION, and reports the result.
//
// What this deliberately does NOT do: download anything, install anything, or
// run anything. The app ships as a .deb and installing one needs root. Silently
// self-updating a root-installed package from a desktop session is a bad idea,
// so the most this offers is a link to the release page. "OTA" here means "you
// are told", not "it happens to you".
//
// The check is off the startup path: it is fired after the window is up, it is
// throttled to once a day, it can be turned off, and every failure is silent.
// A machine with no network must never see an error it did not ask for.
#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace xen {

// Parsed semantic version. Enough for "v1.2.3" and "1.2.3-rc1"; anything it
// cannot parse compares as invalid and is treated as "no update".
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    QString pre;          // pre-release suffix, empty for a final release
    bool valid = false;

    static Version parse(const QString& text);
    // Ordering follows semver: 1.2.3-rc1 sorts before 1.2.3.
    bool isNewerThan(const Version& other) const;
    [[nodiscard]] QString toString() const;
};

class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject* parent = nullptr);

    // The version this build reports as its own.
    static Version currentVersion();

    // User preference. Default is on; the UI states plainly what it does.
    static bool enabled();
    static void setEnabled(bool on);

    // A version the user chose to stop being told about.
    static QString skippedVersion();
    static void setSkippedVersion(const QString& tag);

    // Fire a check. `manual` bypasses both the daily throttle and the enabled
    // setting, because it can only be reached by the user clicking a button.
    // An automatic check that is throttled, disabled, or already running is a
    // silent no-op.
    void check(bool manual = false);

    [[nodiscard]] bool busy() const { return m_reply != nullptr; }

signals:
    // A newer release exists and the user has not skipped it.
    void updateAvailable(const QString& version, const QString& htmlUrl);
    // Ran to completion and this build is current (or newer than the release).
    void upToDate(const QString& version);
    // Only ever emitted for a manual check. Automatic failures stay quiet.
    void checkFailed(const QString& error);

private:
    void onFinished(QNetworkReply* reply, bool manual);

    QNetworkAccessManager* m_net = nullptr;
    QNetworkReply* m_reply = nullptr;
};

} // namespace xen
