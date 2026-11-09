/*
 * Tape side of the Studio live link.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "studiolink.h"

#include <cmath>

#include "studiolinkpeer.h"

using namespace au::instruments;
using au::playback::PlaybackStatus;

StudioLink::StudioLink(const muse::modularity::ContextPtr& ctx)
    : muse::Contextable(ctx)
{
}

StudioLink::~StudioLink() = default;

void StudioLink::init()
{
    m_peer = std::make_unique<studiolink::Peer>("tape");
    QObject::connect(m_peer.get(), &studiolink::Peer::received, m_peer.get(), [this](const QJsonObject& m) { onMessage(m); });

    m_seekDebounce.setSingleShot(true);
    m_seekDebounce.setInterval(120);
    QObject::connect(&m_seekDebounce, &QTimer::timeout, m_peer.get(), [this]() { sendState("seek"); });

    subscribe();
    globalContext()->currentProjectChanged().onNotify(this, [this]() { subscribe(); });

    m_peer->start();
}

void StudioLink::subscribe()
{
    auto st = globalContext()->playbackState();
    if (!st) {
        return;
    }
    st->playbackStatusChanged().onReceive(this, [this](PlaybackStatus) {
        const bool playing = isPlaying();
        if (playing != m_wasPlaying) {
            m_wasPlaying = playing;
            if (!muted()) {
                sendState(playing ? "play" : "stop");
            }
        }
    }, muse::async::Asyncable::Mode::SetReplace);
    st->playbackPositionChanged().onReceive(this, [this](muse::secs_t) {
        if (!isPlaying() && !muted()) {
            m_seekDebounce.start();
        }
    }, muse::async::Asyncable::Mode::SetReplace);
}

bool StudioLink::isPlaying() const
{
    auto st = globalContext()->playbackState();
    return st && st->playbackStatus() == PlaybackStatus::Running;
}

double StudioLink::position() const
{
    auto st = globalContext()->playbackState();
    return st ? st->playbackPosition().raw() : 0.0;
}

bool StudioLink::muted() const
{
    return m_applied.isValid() && m_applied.elapsed() < 600;
}

void StudioLink::sendState(const char* cmd)
{
    const double t = position();
    if (std::string(cmd) == "seek" && std::fabs(t - m_lastSentPos) < 0.01) {
        return;
    }
    m_lastSentPos = t;
    m_peer->send(QJsonObject { { "cmd", cmd }, { "t", t } });
}

void StudioLink::onMessage(const QJsonObject& msg)
{
    auto player = playback() ? playback()->player() : nullptr;
    if (!player || !globalContext()->currentProject()) {
        return;
    }
    const QString cmd = msg.value("cmd").toString();
    const double t = std::max(0.0, msg.value("t").toDouble());
    m_applied.start();

    if (cmd == "play") {
        if (isPlaying()) {
            player->pause();
        }
        player->seek(muse::secs_t(t));
        player->play();
        m_wasPlaying = true;
    } else if (cmd == "stop") {
        if (isPlaying()) {
            player->pause();
        }
        player->seek(muse::secs_t(t));
        m_wasPlaying = false;
    } else if (cmd == "seek") {
        if (!isPlaying()) {
            player->seek(muse::secs_t(t));
        }
    } else if (cmd == "hello") {
        m_applied.invalidate();
        sendState(isPlaying() ? "play" : "stop");
    }
    m_lastSentPos = t;
}
