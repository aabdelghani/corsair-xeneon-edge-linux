// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/MprisWatcher.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QHash>
#include <QStringList>
#include <QVariantMap>

namespace xen {
namespace {

const QLatin1String kPrefix("org.mpris.MediaPlayer2.");
const QLatin1String kPath("/org/mpris/MediaPlayer2");
const QLatin1String kPlayer("org.mpris.MediaPlayer2.Player");

// A property read with a deadline. A wedged player must not be able to stall
// the agent's poll loop, so this waits a few hundred milliseconds and gives up.
QVariant playerProperty(QDBusConnection& bus, const QString& service, const QString& name)
{
    QDBusMessage call = QDBusMessage::createMethodCall(
        service, kPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    call << QString(kPlayer) << name;

    const QDBusMessage reply = bus.call(call, QDBus::Block, 400);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};
    return reply.arguments().constFirst().value<QDBusVariant>().variant();
}

// a{sv} arrives as a QDBusArgument and has to be streamed out; it is not a
// QVariantMap until someone demarshals it.
QVariantMap toMap(const QVariant& v)
{
    QVariantMap out;
    if (v.canConvert<QDBusArgument>()) {
        const QDBusArgument arg = v.value<QDBusArgument>();
        if (arg.currentType() == QDBusArgument::MapType)
            arg >> out;
    } else if (v.canConvert<QVariantMap>()) {
        out = v.toMap();
    }
    return out;
}

// xesam:artist is a list of strings. Players are inconsistent about whether it
// arrives as one, so a bare string is accepted too rather than dropped.
QString joinStrings(const QVariant& v)
{
    if (v.canConvert<QDBusArgument>()) {
        const QDBusArgument arg = v.value<QDBusArgument>();
        QStringList list;
        if (arg.currentType() == QDBusArgument::ArrayType) {
            arg >> list;
            return list.join(QStringLiteral(", "));
        }
        return {};
    }
    if (v.typeId() == QMetaType::QStringList)
        return v.toStringList().join(QStringLiteral(", "));
    return v.toString();
}

QString shortPlayerName(const QString& service)
{
    QString name = service.mid(kPrefix.size());
    // Chromium and friends append ".instance1234", which is noise on a panel.
    const int dot = name.indexOf(QLatin1Char('.'));
    if (dot > 0)
        name.truncate(dot);
    return name;
}

} // namespace

NowPlaying MprisWatcher::read()
{
    NowPlaying np;
    // Forget the last player first, so control() cannot reach one that has
    // since quit and been replaced by nothing.
    m_lastService.clear();

    QDBusConnection bus = QDBusConnection::sessionBus();
    m_busOk = bus.isConnected();
    if (!m_busOk || !bus.interface())
        return np;

    const QDBusReply<QStringList> names = bus.interface()->registeredServiceNames();
    if (!names.isValid())
        return np;

    QString chosen;
    QString chosenStatus;
    for (const QString& service : names.value()) {
        if (!service.startsWith(kPrefix))
            continue;
        const QString status =
            playerProperty(bus, service, QStringLiteral("PlaybackStatus")).toString();
        if (chosen.isEmpty()) {
            chosen = service;
            chosenStatus = status;
        }
        // Something actually playing wins over something merely open, which is
        // the difference between a paused browser tab from this morning and
        // the album you are listening to now.
        if (status == QLatin1String("Playing")) {
            chosen = service;
            chosenStatus = status;
            break;
        }
    }
    if (chosen.isEmpty())
        return np;

    const QVariantMap md = toMap(playerProperty(bus, chosen, QStringLiteral("Metadata")));
    np.title = md.value(QStringLiteral("xesam:title")).toString();
    np.artist = joinStrings(md.value(QStringLiteral("xesam:artist")));
    np.album = md.value(QStringLiteral("xesam:album")).toString();
    np.artUrl = md.value(QStringLiteral("mpris:artUrl")).toString();
    np.lengthUs = md.value(QStringLiteral("mpris:length"), -1).toLongLong();

    bool ok = false;
    const qint64 pos = playerProperty(bus, chosen, QStringLiteral("Position")).toLongLong(&ok);
    np.positionUs = ok ? pos : -1;

    np.player = shortPlayerName(chosen);
    np.status = chosenStatus;
    // A player with no title is one that has not loaded anything: a browser
    // sitting on a blank tab exports MPRIS with nothing in it, and "playing
    // nothing" is not worth a tile.
    np.valid = !np.title.isEmpty();
    m_lastService = chosen;
    return np;
}

bool MprisWatcher::control(const QString& action, QString* error)
{
    static const QHash<QString, QString> methods{
        { QStringLiteral("previous"), QStringLiteral("Previous") },
        { QStringLiteral("playpause"), QStringLiteral("PlayPause") },
        { QStringLiteral("next"), QStringLiteral("Next") },
    };
    const QString method = methods.value(action);
    if (method.isEmpty()) {
        if (error)
            *error = QStringLiteral("unknown media action '%1'").arg(action);
        return false;
    }
    if (m_lastService.isEmpty()) {
        if (error)
            *error = QStringLiteral("no media player on the session bus");
        return false;
    }

    const QDBusMessage call =
        QDBusMessage::createMethodCall(m_lastService, kPath, kPlayer, method);
    // The same short deadline as the property reads: a wedged player must not
    // stall the agent.
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 800);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (error)
            *error = reply.errorMessage();
        return false;
    }
    return true;
}

} // namespace xen
