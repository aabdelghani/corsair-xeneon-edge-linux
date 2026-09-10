// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/UpdateChecker.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QUrl>

#ifndef EDGELINE_UPDATE_REPO
#define EDGELINE_UPDATE_REPO "aabdelghani/corsair-xeneon-edge-linux"
#endif

namespace xen {
namespace {

constexpr const char* kEnabledKey = "updates/enabled";
constexpr const char* kLastCheckKey = "updates/lastCheckMs";
constexpr const char* kSkippedKey = "updates/skippedVersion";

// Once a day is plenty for a tool that ships a release every few months, and it
// keeps the app from being a rude neighbour to an unauthenticated API.
constexpr qint64 kThrottleMs = 24LL * 60 * 60 * 1000;
constexpr int kTimeoutMs = 8000;

QString apiUrl()
{
    return QStringLiteral("https://api.github.com/repos/%1/releases/latest")
        .arg(QLatin1String(EDGELINE_UPDATE_REPO));
}

} // namespace

Version Version::parse(const QString& text)
{
    // Accepts "v1.2.3", "1.2.3", "v1.2.3-rc1". Trailing junk is not accepted,
    // because guessing at a tag we do not understand is worse than saying no.
    static const QRegularExpression re(
        QStringLiteral("^[vV]?(\\d+)\\.(\\d+)(?:\\.(\\d+))?(?:-([0-9A-Za-z.\\-]+))?$"));
    const QRegularExpressionMatch m = re.match(text.trimmed());
    if (!m.hasMatch())
        return {};

    Version v;
    v.major = m.captured(1).toInt();
    v.minor = m.captured(2).toInt();
    v.patch = m.captured(3).isEmpty() ? 0 : m.captured(3).toInt();
    v.pre = m.captured(4);
    v.valid = true;
    return v;
}

bool Version::isNewerThan(const Version& other) const
{
    if (!valid || !other.valid)
        return false;
    if (major != other.major) return major > other.major;
    if (minor != other.minor) return minor > other.minor;
    if (patch != other.patch) return patch > other.patch;
    // Same numbers: a final release beats a pre-release of the same number.
    if (pre.isEmpty() != other.pre.isEmpty())
        return pre.isEmpty();
    return pre > other.pre;
}

QString Version::toString() const
{
    QString s = QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
    if (!pre.isEmpty())
        s += QLatin1Char('-') + pre;
    return s;
}

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
}

Version UpdateChecker::currentVersion()
{
    return Version::parse(QStringLiteral(EDGELINE_VERSION));
}

bool UpdateChecker::enabled()
{
    return QSettings().value(QLatin1String(kEnabledKey), true).toBool();
}

void UpdateChecker::setEnabled(bool on)
{
    QSettings().setValue(QLatin1String(kEnabledKey), on);
}

QString UpdateChecker::skippedVersion()
{
    return QSettings().value(QLatin1String(kSkippedKey)).toString();
}

void UpdateChecker::setSkippedVersion(const QString& tag)
{
    QSettings().setValue(QLatin1String(kSkippedKey), tag);
}

void UpdateChecker::check(bool manual)
{
    if (m_reply)
        return; // one in flight is enough

    if (!manual) {
        if (!enabled())
            return;
        QSettings s;
        const qint64 last = s.value(QLatin1String(kLastCheckKey), 0).toLongLong();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (last > 0 && now - last < kThrottleMs)
            return;
    }

    QNetworkRequest req{ QUrl(apiUrl()) };
    // GitHub rejects requests with no User-Agent.
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("edgeline/%1").arg(QLatin1String(EDGELINE_VERSION)));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_net->get(req);

    // QNetworkReply has no timeout of its own on Qt 6.4 that covers a stalled
    // connection well enough for a startup path, so bound it ourselves.
    auto* timeout = new QTimer(this);
    timeout->setSingleShot(true);
    timeout->setInterval(kTimeoutMs);
    connect(timeout, &QTimer::timeout, this, [this]() {
        if (m_reply)
            m_reply->abort();
    });
    timeout->start();

    QNetworkReply* reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, manual, timeout]() {
        timeout->stop();
        timeout->deleteLater();
        onFinished(reply, manual);
    });
}

void UpdateChecker::onFinished(QNetworkReply* reply, bool manual)
{
    m_reply = nullptr;
    reply->deleteLater();

    auto quietFail = [&](const QString& why) {
        // Automatic checks never surface an error. A user who is offline, or
        // behind a proxy, or on a network that blocks GitHub, did not ask this
        // app for a diagnosis.
        if (manual)
            emit checkFailed(why);
    };

    if (reply->error() != QNetworkReply::NoError) {
        quietFail(reply->errorString());
        return;
    }

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        quietFail(QStringLiteral("could not parse the release feed"));
        return;
    }

    const QJsonObject obj = doc.object();
    const QString tag = obj.value(QStringLiteral("tag_name")).toString();
    const QString url = obj.value(QStringLiteral("html_url")).toString();
    if (obj.value(QStringLiteral("draft")).toBool()
        || obj.value(QStringLiteral("prerelease")).toBool()) {
        // Only stable releases are offered.
        emit upToDate(currentVersion().toString());
        return;
    }

    const Version latest = Version::parse(tag);
    const Version mine = currentVersion();
    if (!latest.valid) {
        quietFail(QStringLiteral("unrecognised release tag '%1'").arg(tag));
        return;
    }

    // Only record a successful check, so a week of failures does not silently
    // throttle away the next good one.
    QSettings().setValue(QLatin1String(kLastCheckKey),
                         QDateTime::currentMSecsSinceEpoch());

    if (!latest.isNewerThan(mine)) {
        emit upToDate(mine.toString());
        return;
    }
    if (!manual && skippedVersion() == latest.toString()) {
        emit upToDate(mine.toString());
        return;
    }
    emit updateAvailable(latest.toString(),
                         url.isEmpty()
                             ? QStringLiteral("https://github.com/%1/releases/latest")
                                   .arg(QLatin1String(EDGELINE_UPDATE_REPO))
                             : url);
}

} // namespace xen
