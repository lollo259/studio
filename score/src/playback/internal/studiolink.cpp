/*
 * Score side of the Studio live link.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "studiolink.h"

#include <cmath>
#include <functional>

#include "studiolinkpeer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/part.h"
#include "notation/inotationsolomutestate.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/staff.h"
#include "notation/iexcerptnotation.h"
#include "notation/imasternotation.h"
#include "notation/inotationelements.h"
#include "notation/inotationundostack.h"
#include "project/projectcommands.h"
#include <QUrl>
#include <QCoreApplication>
#include <QEvent>

#include "studioclick.h"
#include "studiocues.h"
#include "notation/studiovideocues.h"
#include "notation/inotation.h"

namespace {
// any click or key press in this Score window makes it the one the transport talks to
class InputWatch : public QObject
{
public:
    explicit InputWatch(std::function<void()> f)
        : m_f(std::move(f)) {}
    bool eventFilter(QObject* o, QEvent* e) override
    {
        if (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::KeyPress) {
            m_f();
        }
        return QObject::eventFilter(o, e);
    }

private:
    std::function<void()> m_f;
};
}

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
    // a wait cue stopped playback (or playback went on): Tape listens to the mic meanwhile
    studioMicListen() = [this](bool on) {
        if (m_front || !m_otherFront) {
            m_peer->send(QJsonObject { { "cmd", "listen" }, { "on", on } });
        }
    };
    studioCueWaitingChanged() = [this](bool waiting) {
        if (m_front || !m_otherFront) {
            m_peer->send(QJsonObject { { "cmd", "waiting" }, { "on", waiting } });
        }
    };

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
        if (playing) {
            becomeFront(); // playing here: this is the score the user is working on
        }
        if (!muted() && (m_front || !m_otherFront)) {
            sendState(playing ? "play" : "stop");
        }
    });
    state()->playbackPositionChanged().onReceive(this, [this](muse::audio::secs_t) {
        // the cursor moved by hand while stopped: let Tape follow
        if (!state()->isPlaying() && !muted() && (m_front || !m_otherFront)) {
            m_seekDebounce.start();
        }
    });

    m_peer->start();

    m_inputWatch = std::make_unique<InputWatch>([this]() { becomeFront(); });
    qApp->installEventFilter(m_inputWatch.get());

    projectSession()->masterNotationChanged().onNotify(this, [this]() {
        m_tapeFolder.clear(); // another score: Tape's tracks belong to the old one
        m_tapeProject.clear();
        m_dirtyParts.clear();
        watchScore();
        // inside Studio every score you open is ready in Tape too: its instruments become tracks
        if (qEnvironmentVariableIsSet("STUDIO_EMBEDDED") && projectSession()->masterNotation()) {
            QTimer::singleShot(1500, m_peer.get(), [this]() {
                if (m_tapeFolder.isEmpty() && projectSession()->masterNotation()) {
                    commandDispatcher()->dispatch(project::PROJECT_SEND_TO_TAPE_AUTO_COMMAND);
                }
            });
        }
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
        if (ex->notation()->elements() && isStudioTapeOnly(ex->notation()->elements()->msScore())) {
            return; // the Tape instrument: Tape plays it
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
    for (const auto& ex : master->potentialExcerpts()) { // instruments added since: they get their own track
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
    // the click follows tempo changes: rewrite it, and send it only when it changed
    if (master->masterScore()) {
        const QString click = m_tapeFolder + "/" + m_tapeTitle + "-Click.wav";
        QFile old(click);
        const QByteArray before = old.open(QIODevice::ReadOnly) ? old.readAll() : QByteArray();
        old.close();
        if (writeClickTrack(master->masterScore(), click)) {
            QFile now(click);
            if (now.open(QIODevice::ReadOnly) && now.readAll() != before) {
                files << QDir::toNativeSeparators(click);
            }
        }
    }
    m_dirtyParts.clear();
    m_rendering = false;

    m_peer->send(QJsonObject { { "cmd", "tempo" }, { "points", studioTempoPoints(master) } });
    m_peer->send(QJsonObject { { "cmd", "videocues" }, { "cues", studioVideoCues(master) }, { "waits", studioWaitTimes(master) } });
    if (!files.isEmpty()) {
        m_peer->send(QJsonObject { { "cmd", "reload" }, { "files", QJsonArray::fromStringList(files) }, { "tapeProject", m_tapeProject } });
    }
}

void StudioLink::setSilent(bool silent)
{
    if (!audioPlayback()) {
        return;
    }
    audioPlayback()->masterParams().onResolve(this, [this, silent](const muse::audio::TrackParams& p) {
        muse::audio::ControlParams c = p.control;
        if (c.muted != silent) {
            c.muted = silent;
            audioPlayback()->setMasterControlParams(c);
        }
    });
}

void StudioLink::applyReplaced(const QJsonArray& parts)
{
    auto master = projectSession()->masterNotation();
    if (!master || !master->notation() || !master->notation()->soloMuteState()) {
        return;
    }
    QSet<QString> wanted;
    for (const auto& v : parts) {
        wanted.insert(v.toString());
    }
    auto smState = master->notation()->soloMuteState();
    mu::engraving::MasterScore* score = master->masterScore();
    for (const mu::engraving::Part* part : score ? score->parts() : std::vector<mu::engraving::Part*> {}) {
        const QString name = QString::fromStdString(muse::io::escapeFileName(muse::io::path_t(part->partName())).toStdString());
        const bool replaced = wanted.contains(name);
        const bool wasOurs = m_replacedParts.contains(name);
        if (!replaced && !wasOurs) {
            continue; // never touch what the user muted by hand
        }
        for (const auto& [instrId, instr] : part->instruments()) {
            Q_UNUSED(instrId);
            mu::engraving::InstrumentTrackId id { part->id(), instr->id() };
            auto st = smState->trackSoloMuteStateExists(id) ? smState->trackSoloMuteState(id)
                      : notation::INotationSoloMuteState::SoloMuteState();
            if (st.mute != replaced) {
                st.mute = replaced;
                smState->setTrackSoloMuteState(id, st);
            }
        }
    }
    m_replacedParts = wanted;
}

void StudioLink::becomeFront()
{
    if (m_front) {
        return;
    }
    m_front = true;
    m_otherFront = false;
    m_peer->send(QJsonObject { { "cmd", "front" }, { "pid", qint64(QCoreApplication::applicationPid()) } });
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

    if (cmd == "front") {
        if (qint64(msg.value("pid").toDouble()) != qint64(QCoreApplication::applicationPid())) {
            m_front = false;
            m_otherFront = true; // another Score window is the one in use
        }
        return;
    }
    // transport and Tape refreshes go to the Score window in use only
    if (m_otherFront && !m_front) {
        return;
    }
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
        m_tapeProject = msg.value("tapeProject").toString();
        m_dirtyParts.clear();
        watchScore();
        m_applied.invalidate();
        return;
    } else if (cmd == "onset") {
        // Follow me with a live instrument: Tape heard a note on the mic
        if (studioOnset()) {
            studioOnset()();
        }
        m_applied.invalidate();
        return;
    } else if (cmd == "cue") {
        // Tape heard a note on the mic while playback waits at a cue: go on
        if (studioCueWaiting() && !state()->isPlaying()) {
            controller()->play(false);
        }
        m_applied.invalidate();
        return;
    } else if (cmd == "hitpoints") {
        // Tape: cues placed on the video - drawn in the bars at their exact place
        auto& pts = mu::notation::studio::videoHitPoints();
        pts.clear();
        int n = 0;
        for (const QJsonValue& v : msg.value("points").toArray()) {
            pts.push_back({ v.toObject().value("t").toDouble(), ++n });
        }
        if (auto master = projectSession()->masterNotation()) {
            master->notation()->notationChanged().send({});
        }
        m_applied.invalidate();
        return;
    } else if (cmd == "settempo") {
        // Tape: a point on the tempo strip was dragged
        if (studioSetTempo(projectSession()->masterNotation(), msg.value("tick").toInt(), msg.value("bpm").toDouble())) {
            m_dirtyParts.insert("*");
            m_peer->send(QJsonObject { { "cmd", "tempo" }, { "points", studioTempoPoints(projectSession()->masterNotation()) } });
            refreshTape();
        }
        m_applied.invalidate();
        return;
    } else if (cmd == "notes") {
        // Tape: "Write last take into the score"
        writeTakeIntoScore(msg.value("midi").toString(), msg.value("at").toDouble(), msg.value("part").toString());
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
        // the Tape page (or both side by side) plays Tape's tracks: Score only moves its cursor
        const QString page = msg.value("page").toString();
        setSilent(page == "tape" || page == "split");
        // turning to Tape: bring its tracks up to date with the edits made since
        if (page == "tape") {
            QTimer::singleShot(50, m_peer.get(), [this]() {
                if (m_tapeFolder.isEmpty()) {
                    // this score isn't in Tape yet: put all its instruments there now
                    commandDispatcher()->dispatch(project::PROJECT_SEND_TO_TAPE_AUTO_COMMAND);
                } else {
                    if (!m_tapeProject.isEmpty()) { // Tape shows this score's project
                        m_peer->send(QJsonObject { { "cmd", "project" }, { "tapeProject", m_tapeProject } });
                    }
                    // the score's video cues drive Tape's video window
                    m_peer->send(QJsonObject { { "cmd", "videocues" }, { "cues", studioVideoCues(projectSession()->masterNotation()) },
                                       { "waits", studioWaitTimes(projectSession()->masterNotation()) } });
                    m_peer->send(QJsonObject { { "cmd", "tempo" }, { "points", studioTempoPoints(projectSession()->masterNotation()) } });
                    refreshTape();
                }
            });
        }
        m_applied.invalidate();
        return;
    } else if (cmd == "replace") {
        // Studio: these instruments have a recording in Tape - Score mutes them, Tape plays the recordings
        applyReplaced(msg.value("parts").toArray());
        m_applied.invalidate();
        return;
    } else if (cmd == "hello") {
        m_applied.invalidate();
        sendState(state()->isPlaying() ? "play" : "stop");
        m_peer->send(QJsonObject { { "cmd", "videocues" }, { "cues", studioVideoCues(projectSession()->masterNotation()) },
                                       { "waits", studioWaitTimes(projectSession()->masterNotation()) } });
        m_peer->send(QJsonObject { { "cmd", "tempo" }, { "points", studioTempoPoints(projectSession()->masterNotation()) } });
    }
    m_lastSentPos = t;
}
