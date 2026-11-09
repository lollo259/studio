/*
 * Score side of the Studio live link.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "studiolink.h"

#include <cmath>

#include "studiolinkpeer.h"

using namespace mu::playback;

StudioLink::StudioLink(const muse::modularity::ContextPtr& ctx)
    : muse::Contextable(ctx)
{
}

StudioLink::~StudioLink() = default;

void StudioLink::init()
{
    m_peer = std::make_unique<studiolink::Peer>("score");
    QObject::connect(m_peer.get(), &studiolink::Peer::received, m_peer.get(), [this](const QJsonObject& m) { onMessage(m); });

    m_seekDebounce.setSingleShot(true);
    m_seekDebounce.setInterval(120);
    QObject::connect(&m_seekDebounce, &QTimer::timeout, m_peer.get(), [this]() { sendState("seek"); });

    m_posTimer.setInterval(100);
    QObject::connect(&m_posTimer, &QTimer::timeout, m_peer.get(), [this]() {
        m_peer->send(QJsonObject { { "cmd", "pos" }, { "t", state()->playbackPosition().raw() } });
    });

    state()->isPlayingChanged().onReceive(this, [this](bool playing) {
        if (playing) {
            m_posTimer.start();
        } else {
            m_posTimer.stop();
        }
        if (!muted()) {
            sendState(playing ? "play" : "stop");
        }
    });
    state()->playbackPositionChanged().onReceive(this, [this](muse::audio::secs_t) {
        // the cursor moved by hand while stopped: let Tape follow
        if (!state()->isPlaying() && !muted()) {
            m_seekDebounce.start();
        }
    });

    m_peer->start();
}

bool StudioLink::muted() const
{
    return m_applied.isValid() && m_applied.elapsed() < 600;
}

void StudioLink::sendState(const char* cmd)
{
    const double t = state()->playbackPosition().raw();
    if (std::string(cmd) == "seek" && std::fabs(t - m_lastSentPos) < 0.01) {
        return;
    }
    m_lastSentPos = t;
    m_peer->send(QJsonObject { { "cmd", cmd }, { "t", t } });
}

void StudioLink::rewindTo(double secs)
{
    controller()->rewind(muse::secs_t(secs));
}

void StudioLink::onMessage(const QJsonObject& msg)
{
    const QString cmd = msg.value("cmd").toString();
    const double t = std::max(0.0, msg.value("t").toDouble());
    m_applied.start();

    if (cmd == "play") {
        if (state()->isPlaying()) {
            controller()->pause();
        }
        rewindTo(t);
        controller()->play(false);
    } else if (cmd == "stop") {
        if (state()->isPlaying()) {
            controller()->pause();
        }
        rewindTo(t);
    } else if (cmd == "seek") {
        if (!state()->isPlaying()) {
            rewindTo(t);
        }
    } else if (cmd == "hello") {
        m_applied.invalidate();
        sendState(state()->isPlaying() ? "play" : "stop");
    }
    m_lastSentPos = t;
}
