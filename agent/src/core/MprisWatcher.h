// edgeline: what is playing, from any MPRIS player on the session bus.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// MPRIS is the one piece of this that every Linux media player agrees on:
// browsers, Spotify, mpv, Rhythmbox and VLC all export
// org.mpris.MediaPlayer2.<something> with the same properties, so reading it
// needs no per-application knowledge.
//
// Polled rather than signal driven. The sensor snapshot is rebuilt once a
// second regardless, these are local IPC calls on a socket, and polling means
// a player that appears or disappears needs no subscription bookkeeping and
// cannot leave a stale subscription behind when it exits.
#pragma once

#include <QString>

namespace xen {

struct NowPlaying {
    bool valid = false;
    QString player;      // "chromium", from the tail of the bus name
    QString status;      // "Playing", "Paused" or "Stopped"
    QString title;
    QString artist;
    QString album;
    QString artUrl;      // file:// or https://, whatever the player published
    qint64 positionUs = -1;
    qint64 lengthUs = -1;
};

class MprisWatcher {
public:
    // The player currently playing, or the first one found if none is. Returns
    // an invalid NowPlaying when no player is on the bus, which is the normal
    // state on a machine with nothing open.
    [[nodiscard]] NowPlaying read();

    // False when there is no session bus at all, which is the difference
    // between "nothing is playing" and "this cannot work here". The panel
    // needs to tell those apart to stay honest.
    [[nodiscard]] bool busAvailable() const { return m_busOk; }

    // Transport control on the player read() last chose: "previous",
    // "playpause" or "next". These are ordinary method calls on the player's
    // own interface, sent on the normal session connection.
    bool control(const QString& action, QString* error);

private:
    bool m_busOk = false;
    QString m_lastService;   // the player read() chose, for control()
};

} // namespace xen
