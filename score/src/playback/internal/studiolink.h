/*
 * Score side of the Studio live link: play, stop and the cursor are shared with Tape.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QTimer>
#include <memory>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "rcommand/icommanddispatcher.h"
#include "../iplaybackcontroller.h"

namespace studiolink {
class Peer;
}

namespace mu::playback {
class StudioLink : public muse::Contextable, public muse::async::Asyncable
{
    muse::ContextInject<IPlaybackController> controller = { this };
    muse::ContextInject<IPlaybackState> state = { this };
    muse::ContextInject<muse::rcommand::ICommandDispatcher> commandDispatcher = { this };

public:
    StudioLink(const muse::modularity::ContextPtr& ctx);
    ~StudioLink();

    void init();

private:
    void onMessage(const QJsonObject& msg);
    void sendState(const char* cmd);
    bool muted() const;
    void rewindTo(double secs);

    std::unique_ptr<studiolink::Peer> m_peer;
    QElapsedTimer m_applied;   // when we last applied a message (to not echo it back)
    QTimer m_seekDebounce;
    QTimer m_posTimer;
    double m_lastSentPos = -1;
};
}
