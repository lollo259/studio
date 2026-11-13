/*
 * Tape (Laurence Who Studio) — instruments
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "instrumenttracks.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QSettings>
#include <QStandardPaths>

#include "framework/actions/actiontypes.h"
#include "project/iaudacityproject.h"

#include "../instrumentsmodule.h"
#include "../../scenesapp/scenesformat.h"
#include "instrumentsservice.h"

using namespace au::instruments;

namespace {
// shared by every InstrumentTracks (panel, menu actions)
QString s_lastMidi;
}

QString InstrumentTracks::prefix()
{
    return QString::fromUtf8("Instrument · ");
}

bool InstrumentTracks::isInstrumentTitle(const QString& title)
{
    return title.startsWith(QStringLiteral("Instrument"));
}

void InstrumentTracks::addInstrumentTrack()
{
    auto project = globalContext()->currentTrackeditProject();
    if (!project) {
        // no project yet: just open the instrument
        interactive()->open("audacity://instruments");
        return;
    }

    dispatcher()->dispatch("new-stereo-track");
    project = globalContext()->currentTrackeditProject();
    const std::vector<trackedit::TrackId> ids = project ? project->trackIdList() : std::vector<trackedit::TrackId> {};
    if (!ids.empty()) {
        QString sound;
        if (auto s = InstrumentsModule::service()) {
            sound = QString::fromStdString(s->synth()->currentSoundName());
        }
        const trackedit::TrackId id = ids.back();
        trackeditInteraction()->changeTrackTitle(id, muse::String::fromQString(prefix() + (sound.isEmpty() ? QString("Piano") : sound)));
        selectionController()->setSelectedTracks({ id });
    }
    interactive()->open("audacity://instruments");
}

au::trackedit::TrackId InstrumentTracks::targetTrack() const
{
    auto project = globalContext()->currentTrackeditProject();
    if (!project) {
        return -1;
    }
    // the selected instrument track, else the last instrument track in the project
    for (trackedit::TrackId id : selectionController()->selectedTracks()) {
        auto t = project->track(id);
        if (t && isInstrumentTitle(t->title.toQString())) {
            return id;
        }
    }
    const std::vector<trackedit::Track> tracks = project->trackList();
    for (auto it = tracks.rbegin(); it != tracks.rend(); ++it) {
        if (isInstrumentTitle(it->title.toQString())) {
            return it->id;
        }
    }
    return -1;
}

bool InstrumentTracks::startTake(const QString& folder, const QString& name)
{
    auto s = InstrumentsModule::service();
    if (!s) {
        return false;
    }
    if (!s->synth()->isRunning()) {
        s->synth()->start();
    }

    m_takeTrack = targetTrack();
    m_startedPlayback = false;
    m_takeStart = 0.0;
    if (m_takeTrack >= 0) {
        // play the project along from the cursor, so the take sits in time with the other tracks
        auto state = globalContext()->playbackState();
        m_takeStart = state ? state->playbackPosition().raw() : 0.0;
        if (!playbackController()->isPlaying()) {
            dispatcher()->dispatch(muse::actions::ActionQuery("action://playback/toggle-play-pause"));
            m_startedPlayback = true;
        }
    }
    return s->synth()->startTake(folder.toStdString(), name.toStdString());
}

bool InstrumentTracks::isTaking() const
{
    auto s = InstrumentsModule::service();
    return s && s->synth()->isTaking();
}

QString InstrumentTracks::stopTake()
{
    auto s = InstrumentsModule::service();
    if (!s || !s->synth()->isTaking()) {
        return QString();
    }
    TakeResult r = s->synth()->stopTake();
    m_lastTakeStart = m_takeStart;
    m_lastTakeTrack = m_takeTrack;
    if (m_startedPlayback) {
        playbackController()->stop();
        m_startedPlayback = false;
    }
    const QString audio = QString::fromStdString(r.audioPath);
    if (!r.midiPath.empty()) {
        s_lastMidi = QString::fromStdString(r.midiPath);
        // Studio live link: Score opens the take's notes straight away, only if switched on
        // (off by default: Score quantizes the notes; Record > Open last take in Score does it by hand)
        QLocalSocket sock;
        const bool toScore = QSettings("LaurenceWho", "Studio").value("takesToScore", false).toBool();
        if (toScore) {
            sock.connectToServer("laurencewho-studio-link");
        }
        if (toScore && sock.waitForConnected(200)) {
            QJsonObject take { { "cmd", "take" }, { "midi", QDir::toNativeSeparators(s_lastMidi) }, { "from", "tape-take" } };
            sock.write(QJsonDocument(take).toJson(QJsonDocument::Compact) + "\n");
            sock.flush();
            sock.waitForBytesWritten(300);
            sock.disconnectFromServer();
        }
    }
    if (audio.isEmpty() || r.seconds <= 0.05) {
        return audio;
    }

    auto project = globalContext()->currentProject();
    if (project && m_takeTrack >= 0) {
        project->importIntoTrack(muse::io::path_t(audio), m_takeTrack, muse::secs_t(m_takeStart));
    } else {
        // no instrument track: the take becomes a new track
        dispatcher()->dispatch("project-import", muse::actions::ActionData::make_arg1<QStringList>(QStringList { audio }));
    }
    m_takeTrack = -1;
    return audio;
}

QString InstrumentTracks::findScore()
{
    QSettings settings("LaurenceWho", "Studio");
    const QString saved = settings.value("scorePath").toString();
    if (!saved.isEmpty() && QFileInfo::exists(saved)) {
        return saved;
    }
    // Studio folder: Studio/Tape/bin/Tape.exe and Studio/Score/bin/Score.exe
    const QDir bin(QCoreApplication::applicationDirPath());
    const QStringList candidates = {
        bin.filePath("../../Score/bin/Score.exe"),
        bin.filePath("../../Score/bin/LaurenceWhoScore5.exe"),
        bin.filePath("../../Score/bin/mscore"),
    };
    for (const QString& c : candidates) {
        if (QFileInfo(c).isFile()) {
            return QFileInfo(c).canonicalFilePath();
        }
    }
    return QString();
}

bool InstrumentTracks::openLastTakeInScore(QString* error)
{
    if (s_lastMidi.isEmpty() || !QFileInfo::exists(s_lastMidi)) {
        if (error) {
            *error = "Record a take first: its notes open in Score.";
        }
        return false;
    }
    QString score = findScore();
    if (score.isEmpty()) {
        muse::io::path_t picked = interactive()->selectOpeningFileSync(
            "Where is Score? Pick Score.exe",
            muse::io::path_t(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)),
            { "Score (*.exe)", "All files (*)" });
        if (picked.empty()) {
            return false;
        }
        score = picked.toQString();
        QSettings("LaurenceWho", "Studio").setValue("scorePath", score);
    }
    if (!QProcess::startDetached(score, { QDir::toNativeSeparators(s_lastMidi) }, QFileInfo(score).absolutePath())) {
        QSettings("LaurenceWho", "Studio").remove("scorePath");
        if (error) {
            *error = "Score didn't start.";
        }
        return false;
    }
    return true;
}

QString InstrumentTracks::findScenes()
{
    // Scenes.exe is built with Tape and sits next to it in Tape/bin
    const QDir bin(QCoreApplication::applicationDirPath());
    for (const QString& name : { QStringLiteral("Scenes.exe"), QStringLiteral("Scenes") }) {
        const QString p = bin.filePath(name);
        if (QFileInfo(p).isFile()) {
            return p;
        }
    }
    return QString();
}

bool InstrumentTracks::exportToScenes(QString* error)
{
    auto s = InstrumentsModule::service();
    if (!s) {
        return false;
    }
    const std::vector<SoundEntry>& sounds = s->sounds();
    auto findSound = [&](const QString& name) -> const SoundEntry* {
        for (const SoundEntry& e : sounds) {
            if (QString::fromStdString(e.name).compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
                return &e;
            }
        }
        return nullptr;
    };
    const SoundEntry* current = findSound(QString::fromStdString(s->synth()->currentSoundName()));

    scenes::SceneSet set;
    auto project = globalContext()->currentProject();
    set.name = project ? project->displayName() : QString();
    if (set.name.isEmpty()) {
        set.name = "Tape";
    }

    // each instrument track = one scene with that track's sound on all keys;
    // a track named like a sound (Score's tracks are named after their instruments) counts too
    if (auto te = globalContext()->currentTrackeditProject()) {
        for (const auto& t : te->trackList()) {
            const QString title = t.title.toQString();
            const bool instrument = isInstrumentTitle(title);
            QString soundName = title;
            if (instrument) {
                soundName = title.startsWith(prefix()) ? title.mid(prefix().size()) : title.mid(QStringLiteral("Instrument").size());
            }
            const SoundEntry* e = findSound(soundName);
            if (!e && instrument) {
                e = current;
            }
            if (!e) {
                continue;
            }
            scenes::Scene sc;
            sc.name = soundName.trimmed().isEmpty() ? QString::fromStdString(e->name) : soundName.trimmed();
            scenes::Layer l;
            l.sound = scenes::refFor(*e);
            sc.layers.push_back(l);
            set.scenes.push_back(sc);
        }
    }
    if (set.scenes.empty() && current) {
        scenes::Scene sc;
        sc.name = QString::fromStdString(current->name);
        scenes::Layer l;
        l.sound = scenes::refFor(*current);
        sc.layers.push_back(l);
        set.scenes.push_back(sc);
    }
    if (set.scenes.empty()) {
        if (error) {
            *error = "No instrument tracks to send: add one with Record > New instrument track.";
        }
        return false;
    }

    const QString path = scenes::setPath(set.name);
    QString saveError;
    if (!scenes::saveSet(set, path, &saveError)) {
        if (error) {
            *error = saveError;
        }
        return false;
    }
    const QString exe = findScenes();
    if (exe.isEmpty() || !QProcess::startDetached(exe, { QDir::toNativeSeparators(path) }, QFileInfo(exe).absolutePath())) {
        if (error) {
            *error = "Saved " + QDir::toNativeSeparators(path) + ", but Scenes didn't start (Scenes.exe belongs next to Tape).";
        }
        return false;
    }
    return true;
}
