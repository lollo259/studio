/*
 * Tape (Laurence Who Studio) — instruments
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "instrumenttracks.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

#include "framework/actions/actiontypes.h"
#include "project/iaudacityproject.h"

#include "../instrumentsmodule.h"
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
    if (m_startedPlayback) {
        playbackController()->stop();
        m_startedPlayback = false;
    }
    const QString audio = QString::fromStdString(r.audioPath);
    if (!r.midiPath.empty()) {
        s_lastMidi = QString::fromStdString(r.midiPath);
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
