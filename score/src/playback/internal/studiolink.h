/*
 * Score side of the Studio live link: play, stop and the cursor are shared with Tape.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <memory>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "rcommand/icommanddispatcher.h"
#include "../iplaybackcontroller.h"
#include "audio/main/iplayback.h"
#include "project/iexportprojectscenario.h"
#include "project/iprojectsession.h"

namespace studiolink {
class Peer;
}

namespace mu::playback {
class StudioLink : public muse::Contextable, public muse::async::Asyncable
{
    muse::ContextInject<IPlaybackController> controller = { this };
    muse::ContextInject<IPlaybackState> state = { this };
    muse::ContextInject<muse::audio::IPlayback> audioPlayback = { this };
    muse::ContextInject<muse::rcommand::ICommandDispatcher> commandDispatcher = { this };
    muse::ContextInject<project::IProjectSession> projectSession = { this };
    muse::ContextInject<project::IExportProjectScenario> exportProjectScenario = { this };

public:
    StudioLink(const muse::modularity::ContextPtr& ctx);
    ~StudioLink();

    void init();

private:
    void onMessage(const QJsonObject& msg);
    void sendState(const char* cmd);
    bool muted() const;
    void rewindTo(double secs);

    // keeping Tape's tracks up to date with the score
    void watchScore();
    void refreshTape();

    QString m_tapeFolder;          // where Send to Tape put the instruments
    QString m_tapeTitle;           // their file name start ("<title>-<instrument>.wav")
    QSet<QString> m_dirtyParts;    // part ids edited since the last render ("*" = all)
    bool m_rendering = false;

    std::unique_ptr<studiolink::Peer> m_peer;
    QString m_tapeProject; // Studio: this score's own Tape project
    // Studio: only the page you see makes sound - on the Tape page Score plays silently (cursor only)
    void setSilent(bool silent);
    // Studio: instruments whose sound comes from a recording in Tape (muted here)
    void applyReplaced(const QJsonArray& parts);
    QSet<QString> m_replacedParts;
    // several Score windows can be open (one per file): only the one used last answers play/stop/seek
    bool m_front = false;
    bool m_otherFront = false;
    std::unique_ptr<QObject> m_inputWatch;
    void becomeFront();
    QElapsedTimer m_applied;   // when we last applied a message (to not echo it back)
    QTimer m_seekDebounce;
    QTimer m_posTimer;
    double m_lastSentPos = -1;
};
}
