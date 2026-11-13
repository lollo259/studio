/*
 * Tape side of the Studio live link.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "studiolink.h"

#include <cmath>

#include "studiolinkpeer.h"

#include <QFileInfo>
#include <QJsonArray>

#include "framework/actions/actiontypes.h"

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
    globalContext()->currentProjectChanged().onNotify(this, [this]() {
        subscribe();
        m_savedMutes.clear();
        sendReplaced();
        applyMutes();
    });

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

    // a track renamed to "... · recording" (or back), added or removed: tell Score, set the mutes
    if (auto te = globalContext()->currentTrackeditProject()) {
        te->trackChanged().onReceive(this, [this](const au::trackedit::Track&) {
            sendReplaced();
            applyMutes();
        }, muse::async::Asyncable::Mode::SetReplace);
        te->trackAdded().onReceive(this, [this](const au::trackedit::Track&) {
            sendReplaced();
            applyMutes();
        }, muse::async::Asyncable::Mode::SetReplace);
        te->trackRemoved().onReceive(this, [this](const au::trackedit::Track&) {
            sendReplaced();
        }, muse::async::Asyncable::Mode::SetReplace);
    }
}

// the instruments that have a recording: tracks called "<instrument> · recording"
QStringList StudioLink::replacedParts() const
{
    QStringList parts;
    auto te = globalContext()->currentTrackeditProject();
    for (const auto& t : te ? te->trackList() : std::vector<au::trackedit::Track> {}) {
        const QString title = t.title.toQString();
        if (title.endsWith(recordingSuffix())) {
            parts << title.left(title.size() - recordingSuffix().size()).trimmed();
        }
    }
    parts.removeDuplicates();
    return parts;
}

void StudioLink::sendReplaced()
{
    QJsonArray arr;
    for (const QString& p : replacedParts()) {
        arr.append(p);
    }
    m_peer->send(QJsonObject { { "cmd", "replace" }, { "parts", arr } });
}

// Score's own rendering of a recorded instrument is muted in Tape; on the Score page, Tape plays
// only the recordings (Score plays the rest), on the Tape page everything comes back as it was
void StudioLink::applyMutes()
{
    auto te = globalContext()->currentTrackeditProject();
    auto project = globalContext()->currentProject();
    if (!te || !project || !trackPlayback()) {
        return;
    }
    const QStringList parts = replacedParts();
    const QString base = QFileInfo(project->path().toQString()).completeBaseName();
    const bool scorePage = m_page == "score";
    for (const auto& t : te->trackList()) {
        const QString title = t.title.toQString();
        const long id = long(t.id);
        if (title.endsWith(recordingSuffix())) {
            trackPlayback()->setMuted(id, false);
            continue;
        }
        bool rendered = false;
        for (const QString& p : parts) {
            if (title == base + "-" + p) {
                rendered = true;
            }
        }
        if (rendered) {
            trackPlayback()->setMuted(id, true); // the recording plays instead
            continue;
        }
        if (scorePage && !parts.isEmpty()) {
            if (!m_savedMutes.contains(id)) {
                m_savedMutes.insert(id, trackPlayback()->muted(id));
            }
            trackPlayback()->setMuted(id, true); // Score plays these
        } else if (m_savedMutes.contains(id)) {
            trackPlayback()->setMuted(id, m_savedMutes.take(id));
        }
    }
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

// Score re-rendered some instruments: put the new audio into the tracks with the same name
void StudioLink::reloadTracks(const QStringList& files)
{
    auto project = globalContext()->currentProject();
    auto te = globalContext()->currentTrackeditProject();
    static int tries = 0;
    if (!project || !te) {
        // no project open yet: start one, then put the score's tracks in it
        if (tries++ > 20) {
            tries = 0;
            return;
        }
        if (tries == 1) {
            dispatcher()->dispatch("file-new");
        }
        QTimer::singleShot(300, m_peer.get(), [this, files]() { reloadTracks(files); });
        return;
    }
    tries = 0;
    QStringList newOnes;
    for (const QString& f : files) {
        const QString name = QFileInfo(f).completeBaseName();
        au::trackedit::TrackId id = -1;
        for (const auto& t : te->trackList()) {
            if (t.title.toQString() == name) {
                id = t.id;
                break;
            }
        }
        if (id < 0) {
            newOnes << f;
            continue;
        }
        trackeditInteraction()->removeTracksData({ id }, muse::secs_t(0.0), muse::secs_t(1.0e7), false);
        project->importIntoTrack(muse::io::path_t(f), id, muse::secs_t(0.0));
    }
    if (!newOnes.isEmpty()) {
        dispatcher()->dispatch("project-import", muse::actions::ActionData::make_arg1<QStringList>(newOnes));
    }
}

void StudioLink::onMessage(const QJsonObject& msg)
{
    // Studio: the score in use has its own Tape project - switch to it first
    const QString tapeProject = msg.value("tapeProject").toString();
    if (!tapeProject.isEmpty()) {
        auto cur = globalContext()->currentProject();
        if (!cur || cur->path().toQString() != tapeProject) {
            dispatcher()->dispatch("studio-score-project", muse::actions::ActionData::make_arg1<QString>(tapeProject));
        }
    }
    if (msg.value("cmd").toString() == "project") {
        return;
    }
    // Studio: only the page you see makes sound - on the Score page Tape just follows the cursor
    if (msg.value("cmd").toString() == "page" || msg.value("cmd").toString() == "show") {
        m_page = msg.value("page").toString();
        applyMutes();
        if (m_page == "score" && isPlaying() && replacedParts().isEmpty()) {
            if (auto pl = playback() ? playback()->player() : nullptr) {
                pl->pause();
            }
            m_wasPlaying = false;
        }
        return;
    }
    if (msg.value("cmd").toString() == "reload") {
        QStringList files;
        for (const auto& v : msg.value("files").toArray()) {
            files << v.toString();
        }
        reloadTracks(files);
        return;
    }

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
        // on the Score page, Score makes the sound - except the recordings that replace instruments
        if (m_page != "score" || !replacedParts().isEmpty()) {
            player->play();
            m_wasPlaying = true;
        }
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
        sendReplaced();
    }
    m_lastSentPos = t;
}
