/*
 * Tape side of the Studio live link: play, stop and the cursor are shared with Score.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <QJsonArray>
#include <QJsonObject>

#include "miccue.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QTimer>
#include <memory>

#include "framework/global/async/asyncable.h"
#include "framework/global/modularity/ioc.h"
#include "framework/actions/iactionsdispatcher.h"
#include "context/iglobalcontext.h"
#include "playback/iplayback.h"
#include "trackedit/itrackeditinteraction.h"
#include "playback/itrackplaybackcontrol.h"

#include <QMap>
#include <QStringList>

namespace studiolink {
class Peer;
}

namespace au::instruments {
class StudioLink : public muse::Contextable, public muse::async::Asyncable
{
    muse::ContextInject<au::context::IGlobalContext> globalContext{ this };
    muse::ContextInject<au::playback::IPlayback> playback{ this };
    muse::ContextInject<au::trackedit::ITrackeditInteraction> trackeditInteraction{ this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher{ this };
    muse::ContextInject<au::playback::ITrackPlaybackControl> trackPlayback{ this };

public:
    // Studio: a track called "<instrument> · recording" plays that instrument in place of Score's sound
    static QString recordingSuffix() { return QStringLiteral(" · recording"); }
    StudioLink(const muse::modularity::ContextPtr& ctx);
    ~StudioLink();

    void init();

private:
    void sendHitPoints();
    QJsonArray m_lastHitPoints;
    std::unique_ptr<MicCue> m_micCue;
    void subscribe();
    void onMessage(const QJsonObject& msg);
    void reloadTracks(const QStringList& files);
    void sendState(const char* cmd);
    bool muted() const;
    bool isPlaying() const;
    double position() const;

    QStringList replacedParts() const;
    void sendReplaced();
    void applyMutes();

    std::unique_ptr<studiolink::Peer> m_peer;
    QMap<long, bool> m_savedMutes; // tracks muted only while the Score page plays (their own state before)
    QString m_page; // Studio page in view ("score", "tape", "split"); empty outside Studio
    QElapsedTimer m_applied;
    QTimer m_seekDebounce;
    double m_lastSentPos = -1;
    bool m_wasPlaying = false;
};
}
