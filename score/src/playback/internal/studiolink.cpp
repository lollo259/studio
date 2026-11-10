/*
 * Score side of the Studio live link.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "studiolink.h"

#include <cmath>

#include "studiolinkpeer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/part.h"
#include "engraving/dom/staff.h"
#include "notation/iexcerptnotation.h"
#include "notation/imasternotation.h"
#include "notation/inotationelements.h"
#include "notation/inotationundostack.h"
#include "project/projectcommands.h"
#include <QUrl>

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

    projectSession()->masterNotationChanged().onNotify(this, [this]() {
        m_tapeFolder.clear(); // another score: Tape's tracks belong to the old one
        m_dirtyParts.clear();
        watchScore();
    });
    watchScore();
}

void StudioLink::watchScore()
{
    auto master = projectSession()->masterNotation();
    if (!master || !master->notation() || !master->notation()->undoStack()) {
        return;
    }
    master->notation()->undoStack()->changesChannel().onReceive(this, [this, master](const mu::engraving::ScoreChanges& ch) {
        if (m_tapeFolder.isEmpty() || m_rendering) {
            return;
        }
        // which instruments were touched (staff range -> parts); anything without a range: all of them
        if (ch.staffIdxFrom == muse::nidx || ch.staffIdxTo == muse::nidx) {
            m_dirtyParts.insert("*");
            return;
        }
        mu::engraving::MasterScore* score = master->masterScore();
        for (size_t i = ch.staffIdxFrom; i <= ch.staffIdxTo && score && i < score->nstaves(); ++i) {
            if (const mu::engraving::Staff* st = score->staff(i)) {
                if (st->part()) {
                    m_dirtyParts.insert(QString::fromStdString(st->part()->id().toStdString()));
                }
            }
        }
    }, muse::async::Asyncable::Mode::SetReplace);
}

// Re-render the instruments edited since the last time and tell Tape to swap their audio.
void StudioLink::refreshTape()
{
    auto master = projectSession()->masterNotation();
    if (m_tapeFolder.isEmpty() || m_dirtyParts.isEmpty() || !master || m_rendering) {
        return;
    }
    m_rendering = true;
    const bool all = m_dirtyParts.contains("*");

    notation::INotationPtrList notations;
    auto consider = [&](const notation::IExcerptNotationPtr& ex) {
        if (!ex || !ex->notation()) {
            return;
        }
        bool touched = all;
        if (!touched) {
            mu::engraving::Score* s = ex->notation()->elements() ? ex->notation()->elements()->msScore() : nullptr;
            for (const mu::engraving::Part* p : s ? s->parts() : std::vector<mu::engraving::Part*> {}) {
                if (m_dirtyParts.contains(QString::fromStdString(p->id().toStdString()))) {
                    touched = true;
                    break;
                }
            }
        }
        if (touched) {
            notations.push_back(ex->notation());
        }
    };
    for (const auto& ex : master->excerpts()) {
        consider(ex);
    }

    QStringList files;
    if (!notations.empty()) {
        // the export names each file "<title>-<part>.wav"; remove the old ones first so nothing asks to replace
        for (const auto& n : notations) {
            QFile::remove(m_tapeFolder + "/" + m_tapeTitle + "-" + QString::fromStdString(muse::io::escapeFileName(muse::io::path_t(n->name())).toStdString()) + ".wav");
        }
        const muse::io::path_t base = m_tapeFolder + "/" + m_tapeTitle + ".wav";
        exportProjectScenario()->exportScores(notations, base, project::INotationWriter::UnitType::PER_PART);
        for (const auto& n : notations) {
            const QString f = m_tapeFolder + "/" + m_tapeTitle + "-" + QString::fromStdString(muse::io::escapeFileName(muse::io::path_t(n->name())).toStdString()) + ".wav";
            if (QFileInfo::exists(f)) {
                files << QDir::toNativeSeparators(f);
            }
        }
    }
    m_dirtyParts.clear();
    m_rendering = false;

    if (!files.isEmpty()) {
        m_peer->send(QJsonObject { { "cmd", "reload" }, { "files", QJsonArray::fromStringList(files) } });
    }
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
    } else if (cmd == "sent") {
        // Send to Tape just put this score's instruments in Tape: keep them up to date from now on
        m_tapeFolder = msg.value("folder").toString();
        m_tapeTitle = msg.value("title").toString();
        m_dirtyParts.clear();
        watchScore();
        m_applied.invalidate();
        return;
    } else if (cmd == "take") {
        // a take finished in Tape: open its notes as a score
        const QString midi = msg.value("midi").toString();
        if (QFileInfo::exists(midi)) {
            commandDispatcher()->dispatch(project::PROJECT_OPEN_COMMAND,
                                          { { "url", muse::Val(QUrl::fromLocalFile(midi).toString()) } });
        }
        m_applied.invalidate();
        return;
    } else if (cmd == "page" || cmd == "show") {
        // turning to Tape: bring its tracks up to date with the edits made since
        if (msg.value("page").toString() == "tape") {
            QTimer::singleShot(50, m_peer.get(), [this]() { refreshTape(); });
        }
        m_applied.invalidate();
        return;
    } else if (cmd == "hello") {
        m_applied.invalidate();
        sendState(state()->isPlaying() ? "play" : "stop");
    }
    m_lastSentPos = t;
}
