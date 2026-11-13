/*
 * Tape (Laurence Who Studio) — video
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "videopanelmodel.h"

#include "instruments/internal/livelink.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <QFileInfo>
#include <QGuiApplication>
#include <QMediaMetaData>
#include <QRect>
#include <QScreen>
#include <QSettings>
#include <QSize>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QWindow>

#include "framework/actions/actiontypes.h"
#include "playback/playbacktypes.h"

using namespace au::instruments;

namespace videopanel {
const char* KEY_GEOMETRY = "video/geometry";

constexpr qint64 DRIFT_MS = 40;          // reseek when the picture is further than this from the playhead
constexpr qint64 RESEEK_GRACE_MS = 500;  // and give a seek (or a start) this long to settle first

const double RATES[] = { 24.0, 25.0, 29.97, 30.0 };

int timecodeBase(double fps)
{
    return std::abs(fps - 29.97) < 0.01 ? 30 : int(std::lround(fps));
}
}

using namespace videopanel;

VideoPanelModel::VideoPanelModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    // the film is silent here: its sound belongs on a track (Import video brings it in)
    m_player.setAudioOutput(nullptr);

    m_tick.setInterval(30);
    connect(&m_tick, &QTimer::timeout, this, [this]() {
        sync(false);
        emit timeChanged();
    });

    m_seekTimer.setSingleShot(true);
    m_seekTimer.setInterval(30);
    connect(&m_seekTimer, &QTimer::timeout, this, [this]() { sync(false); });

    connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus st) {
        if (st == QMediaPlayer::LoadedMedia) {
            // show the playhead's frame straight away
            m_player.pause();
            sync(true);
        }
        emit statusChanged();
    });
    connect(&m_player, &QMediaPlayer::seekableChanged, this, [this](bool seekable) {
        if (seekable) {
            sync(true);
        }
    });
    connect(&m_player, &QMediaPlayer::durationChanged, this, [this]() { emit statusChanged(); });
    connect(&m_player, &QMediaPlayer::metaDataChanged, this, [this]() { emit statusChanged(); });
    connect(&m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString& text) {
        m_error = text;
        emit statusChanged();
    });

    connect(this, &VideoPanelModel::settingsChanged, this, &VideoPanelModel::markersChanged);
    connect(LiveLink::instance(), &LiveLink::changed, this, &VideoPanelModel::liveChanged);
    connect(this, &VideoPanelModel::statusChanged, this, &VideoPanelModel::markersChanged);
    connect(this, &VideoPanelModel::videoChanged, this, &VideoPanelModel::markersChanged);
    connect(VideoStore::instance(), &VideoStore::changed, this, &VideoPanelModel::markersChanged);
    connect(VideoStore::instance(), &VideoStore::changed, this, [this]() {
        if (!m_storing) {
            reloadFromStore();
        }
    });
}

VideoPanelModel::~VideoPanelModel()
{
    m_tick.stop();
    m_player.stop();
}

void VideoPanelModel::load()
{
    subscribe();
    globalContext()->currentProjectChanged().onNotify(this, [this]() {
        subscribe();
        reloadFromStore();
    });
    reloadFromStore();
}

void VideoPanelModel::subscribe()
{
    auto st = globalContext()->playbackState();
    if (!st) {
        return;
    }
    st->playbackStatusChanged().onReceive(this, [this](au::playback::PlaybackStatus) {
        if (tapeRunning()) {
            m_tick.start();
        } else {
            m_tick.stop();
        }
        sync(true);
        emit timeChanged();
        if (m_settings.hasVideo()) {
            LiveLink::instance()->syncPicture(VideoStore::instance()->pictureAt(tapePosition(), m_settings.offset), tapeRunning());
        }
    }, muse::async::Asyncable::Mode::SetReplace);

    st->playbackPositionChanged().onReceive(this, [this](muse::secs_t) {
        if (!tapeRunning()) {
            // scrubbing sends many positions: follow them, one seek per frame at most
            if (!m_seekTimer.isActive()) {
                m_seekTimer.start();
            }
            emit timeChanged();
        }
    }, muse::async::Asyncable::Mode::SetReplace);

    if (tapeRunning()) {
        m_tick.start();
    }
}

QString VideoPanelModel::projectPath() const
{
    auto project = globalContext()->currentProject();
    return project ? project->path().toQString() : QString();
}

void VideoPanelModel::reloadFromStore()
{
    m_settings = globalContext()->currentProject() ? VideoStore::instance()->get(projectPath()) : VideoSettings();
    emit settingsChanged();
    emit videoChanged();
    openSource();
    emit timeChanged();
}

void VideoPanelModel::store()
{
    if (!globalContext()->currentProject()) {
        return;
    }
    m_storing = true;
    VideoStore::instance()->set(projectPath(), m_settings);
    m_storing = false;
}

void VideoPanelModel::openSource()
{
    if (m_settings.file == m_loadedFile) {
        return;
    }
    m_loadedFile = m_settings.file;
    m_error.clear();
    m_player.stop();
    if (m_settings.file.isEmpty()) {
        m_player.setSource(QUrl());
    } else if (!QFileInfo::exists(m_settings.file)) {
        m_player.setSource(QUrl());
        m_error = QString("Video not found: %1").arg(m_settings.file);
    } else {
        m_player.setSource(QUrl::fromLocalFile(m_settings.file));
    }
    emit statusChanged();
}

double VideoPanelModel::tapePosition() const
{
    auto st = globalContext()->playbackState();
    return st ? st->playbackPosition().raw() : 0.0;
}

bool VideoPanelModel::tapeRunning() const
{
    auto st = globalContext()->playbackState();
    return st && st->playbackStatus() == au::playback::PlaybackStatus::Running;
}

// Put the picture where Tape's playhead is
void VideoPanelModel::sync(bool force)
{
    if (m_loadedFile.isEmpty() || !m_player.isSeekable()) {
        return;
    }
    const qint64 duration = m_player.duration();
    const double picture = VideoStore::instance()->pictureAt(tapePosition(), m_settings.offset);
    const qint64 target = qint64(std::llround(picture * 1000.0));
    const bool inside = picture >= 0.0 && (duration <= 0 || target < duration);

    if (!inside) {
        // before the first frame or after the last: hold the nearest frame
        if (m_player.playbackState() != QMediaPlayer::PausedState) {
            m_player.pause(); // paused, not stopped: a stopped player shows no frame
        }
        const qint64 hold = target < 0 ? 0 : std::max<qint64>(0, duration - 1);
        if (force || std::llabs(m_player.position() - hold) > DRIFT_MS) {
            m_player.setPosition(hold);
        }
        return;
    }

    if (tapeRunning()) {
        if (m_player.playbackState() != QMediaPlayer::PlayingState) {
            if (m_player.mediaStatus() == QMediaPlayer::EndOfMedia && duration > 0 && target > duration - 250) {
                return; // the last frames: the film already ended, do not restart it in a loop
            }
            m_player.setPosition(target);
            m_player.play();
            m_lastReseek.restart();
            return;
        }
        if (m_lastReseek.isValid() && m_lastReseek.elapsed() < RESEEK_GRACE_MS) {
            return;
        }
        if (std::llabs(m_player.position() - target) > DRIFT_MS) {
            m_player.setPosition(target);
            m_lastReseek.restart();
        }
        return;
    }

    if (m_player.playbackState() != QMediaPlayer::PausedState) {
        m_player.pause();
    }
    if (force || m_player.position() != target) {
        m_player.setPosition(target);
    }
}

// ── actions ────────────────────────────────────────────────────────

void VideoPanelModel::chooseVideo()
{
    dispatcher()->dispatch("video-import");
}

void VideoPanelModel::clearVideo()
{
    m_settings.file.clear();
    store();
    emit videoChanged();
    openSource();
}

void VideoPanelModel::importSound()
{
    if (!m_settings.hasVideo() || !globalContext()->currentProject()) {
        return;
    }
    dispatcher()->dispatch("project-import", muse::actions::ActionData::make_arg1<QStringList>(QStringList { m_settings.file }));
}

void VideoPanelModel::offsetToPlayhead()
{
    setOffset(tapePosition());
}

bool VideoPanelModel::setOffsetText(const QString& text)
{
    double secs = 0.0;
    if (!parseTimecode(text, m_settings.fps, &secs)) {
        emit settingsChanged(); // put the old text back
        return false;
    }
    setOffset(secs);
    return true;
}

void VideoPanelModel::togglePlay()
{
    dispatcher()->dispatch(muse::actions::ActionQuery("action://playback/toggle-play-stop"));
}

void VideoPanelModel::restoreWindow(QObject* obj)
{
    auto window = qobject_cast<QWindow*>(obj);
    if (!window) {
        return;
    }
    const QRect r = QSettings().value(KEY_GEOMETRY).toRect();
    if (!r.isValid()) {
        return;
    }
    // only where a screen still is (the second monitor may be gone)
    for (QScreen* s : QGuiApplication::screens()) {
        if (s->availableGeometry().intersects(r)) {
            window->setGeometry(r);
            return;
        }
    }
}

void VideoPanelModel::saveWindow(QObject* obj)
{
    auto window = qobject_cast<QWindow*>(obj);
    if (!window || window->visibility() == QWindow::FullScreen) {
        return;
    }
    QSettings().setValue(KEY_GEOMETRY, window->geometry());
}

// ── properties ─────────────────────────────────────────────────────

QObject* VideoPanelModel::videoOutput() const
{
    return m_output;
}

void VideoPanelModel::setVideoOutput(QObject* output)
{
    if (m_output == output) {
        return;
    }
    m_output = output;
    m_player.setVideoOutput(output);
    emit videoOutputChanged();
}

bool VideoPanelModel::hasProject() const
{
    return globalContext()->currentProject() != nullptr;
}

bool VideoPanelModel::hasVideo() const
{
    return m_settings.hasVideo();
}

QString VideoPanelModel::videoName() const
{
    return m_settings.hasVideo() ? QFileInfo(m_settings.file).fileName() : QString();
}

QString VideoPanelModel::videoFile() const
{
    return m_settings.file;
}

QString VideoPanelModel::videoInfo() const
{
    if (!m_settings.hasVideo() || !m_error.isEmpty()) {
        return QString();
    }
    QStringList parts;
    const QMediaMetaData md = m_player.metaData();
    const QSize res = md.value(QMediaMetaData::Resolution).toSize();
    if (res.isValid()) {
        parts << QString("%1 × %2").arg(res.width()).arg(res.height());
    }
    const double rate = md.value(QMediaMetaData::VideoFrameRate).toDouble();
    if (rate > 0.0) {
        parts << QString("%1 fps").arg(rate, 0, 'g', 5);
    }
    if (m_player.duration() > 0) {
        parts << formatTimecode(m_player.duration() / 1000.0, m_settings.fps);
    }
    return parts.join("  ·  ");
}

QString VideoPanelModel::error() const
{
    return m_error;
}

double VideoPanelModel::offset() const
{
    return m_settings.offset;
}

void VideoPanelModel::setOffset(double secs)
{
    if (std::abs(secs - m_settings.offset) < 1e-6) {
        return;
    }
    m_settings.offset = secs;
    store();
    emit settingsChanged();
    sync(true);
    emit timeChanged();
}

QString VideoPanelModel::offsetText() const
{
    return formatTimecode(m_settings.offset, m_settings.fps);
}

double VideoPanelModel::fps() const
{
    return m_settings.fps;
}

void VideoPanelModel::setFps(double fps)
{
    if (std::abs(fps - m_settings.fps) < 1e-6) {
        return;
    }
    m_settings.fps = fps;
    store();
    emit settingsChanged();
    emit statusChanged();
    emit timeChanged();
}

int VideoPanelModel::fpsIndex() const
{
    for (int i = 0; i < 4; ++i) {
        if (std::abs(RATES[i] - m_settings.fps) < 0.001) {
            return i;
        }
    }
    return 1;
}

void VideoPanelModel::setFpsIndex(int index)
{
    if (index >= 0 && index < 4) {
        setFps(RATES[index]);
    }
}

QVariantList VideoPanelModel::fpsOptions() const
{
    QVariantList list;
    for (int i = 0; i < 4; ++i) {
        const double r = RATES[i];
        list << QVariantMap { { "text", r == 29.97 ? QString("29.97 fps") : QString("%1 fps").arg(int(r)) }, { "value", i } };
    }
    return list;
}

QString VideoPanelModel::timecode() const
{
    return formatTimecode(VideoStore::instance()->pictureAt(tapePosition(), m_settings.offset), m_settings.fps);
}

QString VideoPanelModel::tapeTime() const
{
    return formatTimecode(tapePosition(), m_settings.fps);
}

QString VideoPanelModel::pictureState() const
{
    if (!m_settings.hasVideo() || !m_error.isEmpty()) {
        return QString();
    }
    const double picture = VideoStore::instance()->pictureAt(tapePosition(), m_settings.offset);
    if (picture < 0.0) {
        return "BEFORE PICTURE";
    }
    const qint64 duration = m_player.duration();
    if (duration > 0 && picture * 1000.0 >= double(duration)) {
        return "END OF PICTURE";
    }
    return QString();
}

bool VideoPanelModel::playing() const
{
    return tapeRunning();
}

// ── timecode ───────────────────────────────────────────────────────

QString VideoPanelModel::formatTimecode(double secs, double fps)
{
    const bool negative = secs < 0.0;
    const int base = timecodeBase(fps);
    // a hair of tolerance, so 1.0 s at 25 fps is frame 25 and not 24
    const qint64 frames = qint64(std::floor(std::abs(secs) * fps + 1e-6));
    const qint64 ff = frames % base;
    const qint64 total = frames / base;
    return QString("%1%2:%3:%4:%5")
           .arg(negative && frames > 0 ? QString("-") : QString())
           .arg(total / 3600, 2, 10, QChar('0'))
           .arg((total / 60) % 60, 2, 10, QChar('0'))
           .arg(total % 60, 2, 10, QChar('0'))
           .arg(ff, 2, 10, QChar('0'));
}

bool VideoPanelModel::parseTimecode(const QString& text, double fps, double* secs)
{
    QString t = text.trimmed();
    if (t.isEmpty()) {
        return false;
    }
    bool negative = false;
    if (t.startsWith('-')) {
        negative = true;
        t = t.mid(1).trimmed();
    }

    double value = 0.0;
    if (!t.contains(':') && !t.contains(';')) {
        bool ok = false;
        value = QString(t).replace(',', '.').toDouble(&ok);
        if (!ok) {
            return false;
        }
    } else {
        const QStringList parts = QString(t).replace(';', ':').split(':');
        if (parts.size() < 2 || parts.size() > 4) {
            return false;
        }
        qint64 fields[4] = { 0, 0, 0, 0 }; // HH MM SS FF, filled from the right
        for (int i = 0; i < parts.size(); ++i) {
            bool ok = false;
            const qint64 v = parts.at(i).trimmed().toLongLong(&ok);
            if (!ok || v < 0) {
                return false;
            }
            fields[4 - parts.size() + i] = v;
        }
        const int base = timecodeBase(fps);
        if (fields[3] >= base) {
            return false;
        }
        const qint64 frames = ((fields[0] * 60 + fields[1]) * 60 + fields[2]) * base + fields[3];
        value = double(frames) / fps;
    }
    *secs = negative ? -value : value;
    return true;
}

// ── Studio: cues on the video ──────────────────────────────────────

void VideoPanelModel::addHitPoint()
{
    if (!m_settings.hasVideo()) {
        return;
    }
    const double t = tapePosition();
    for (double h : m_settings.hitPoints) {
        if (std::abs(h - t) < 0.01) {
            return;
        }
    }
    m_settings.hitPoints.push_back(t);
    std::sort(m_settings.hitPoints.begin(), m_settings.hitPoints.end());
    store();
    emit settingsChanged();
    emit timeChanged();
}

void VideoPanelModel::removeHitPoint()
{
    if (m_settings.hitPoints.empty()) {
        return;
    }
    const double t = tapePosition();
    auto best = m_settings.hitPoints.begin();
    for (auto it = m_settings.hitPoints.begin(); it != m_settings.hitPoints.end(); ++it) {
        if (std::abs(*it - t) < std::abs(*best - t)) {
            best = it;
        }
    }
    m_settings.hitPoints.erase(best);
    store();
    emit settingsChanged();
    emit timeChanged();
}

// "CUE" while the playhead sits on one of the score's wait cues, "V3" on the third video cue
QString VideoPanelModel::cueFlash() const
{
    const double t = tapePosition();
    for (double w : VideoStore::instance()->waits()) {
        if (t >= w - 0.05 && t < w + 0.6) {
            return "CUE";
        }
    }
    for (size_t i = 0; i < m_settings.hitPoints.size(); ++i) {
        const double h = m_settings.hitPoints[i];
        if (t >= h - 0.05 && t < h + 0.4) {
            return QString("V%1").arg(i + 1);
        }
    }
    return QString();
}

// the marks under the picture: [{x: 0..1 along the film, kind: "wait"|"video", label}]
QVariantList VideoPanelModel::markers() const
{
    QVariantList out;
    const double duration = double(m_player.duration()) / 1000.0;
    if (duration <= 0.0) {
        return out;
    }
    auto add = [&](double tapeTime, const QString& kind, const QString& label) {
        const double pic = VideoStore::instance()->pictureAt(tapeTime, m_settings.offset);
        if (pic < 0.0 || pic > duration) {
            return;
        }
        out.append(QVariantMap { { "x", pic / duration }, { "kind", kind }, { "label", label } });
    };
    for (double w : VideoStore::instance()->waits()) {
        add(w, "wait", QString());
    }
    for (size_t i = 0; i < m_settings.hitPoints.size(); ++i) {
        add(m_settings.hitPoints[i], "video", QString("V%1").arg(i + 1));
    }
    return out;
}

// ── Studio: Live (OBS) ─────────────────────────────────────────────

void VideoPanelModel::openLive()
{
    QString error;
    if (!LiveLink::instance()->openLive(&error) && !error.isEmpty()) {
        m_error = error;
        emit statusChanged();
    }
    if (m_settings.hasVideo()) {
        LiveLink::instance()->setPicture(m_settings.file);
    }
}

void VideoPanelModel::toggleStream() { LiveLink::instance()->toggleStream(); }
void VideoPanelModel::toggleRecord() { LiveLink::instance()->toggleRecord(); }
void VideoPanelModel::setLiveScene(const QString& name) { LiveLink::instance()->setScene(name); }
bool VideoPanelModel::liveConnected() const { return LiveLink::instance()->connected(); }
bool VideoPanelModel::liveStreaming() const { return LiveLink::instance()->streaming(); }
bool VideoPanelModel::liveRecording() const { return LiveLink::instance()->recording(); }
QStringList VideoPanelModel::liveScenes() const { return LiveLink::instance()->scenes(); }
QString VideoPanelModel::liveScene() const { return LiveLink::instance()->currentScene(); }
