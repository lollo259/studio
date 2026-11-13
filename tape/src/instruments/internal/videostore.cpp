/*
 * Tape (Laurence Who Studio) — video
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "videostore.h"

#include <cmath>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

using namespace au::instruments;

namespace videostore {
double validFps(double fps)
{
    static const double rates[] = { 24.0, 25.0, 29.97, 30.0 };
    for (double r : rates) {
        if (std::abs(fps - r) < 0.001) {
            return r;
        }
    }
    return 25.0;
}
}

using namespace videostore;

VideoStore* VideoStore::instance()
{
    static VideoStore* s = new VideoStore();
    return s;
}

bool VideoStore::isProjectFile(const QString& projectPath)
{
    if (projectPath.isEmpty()) {
        return false;
    }
    const QString ext = QFileInfo(projectPath).suffix().toLower();
    return ext == "aup4" || ext == "aup3" || ext == "aup";
}

QString VideoStore::sidecarPath(const QString& projectPath)
{
    const QFileInfo fi(projectPath);
    return fi.absoluteDir().filePath(fi.completeBaseName() + ".studio-video.json");
}

VideoSettings VideoStore::get(const QString& projectPath) const
{
    if (!isProjectFile(projectPath)) {
        return m_unsaved;
    }

    VideoSettings s;
    QFile f(sidecarPath(projectPath));
    if (!f.open(QIODevice::ReadOnly)) {
        return s;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();

    // the video is stored relative to the project when it can be, so a project folder can be moved
    const QDir projectDir = QFileInfo(projectPath).absoluteDir();
    const QString rel = o.value("video").toString();
    const QString abs = o.value("videoAbsolute").toString();
    if (!rel.isEmpty() && QFileInfo::exists(projectDir.absoluteFilePath(rel))) {
        s.file = QFileInfo(projectDir.absoluteFilePath(rel)).absoluteFilePath();
    } else {
        s.file = !abs.isEmpty() ? abs : (rel.isEmpty() ? QString() : projectDir.absoluteFilePath(rel));
    }
    s.offset = o.value("offset").toDouble(0.0);
    s.fps = validFps(o.value("fps").toDouble(25.0));
    return s;
}

void VideoStore::set(const QString& projectPath, const VideoSettings& settings)
{
    VideoSettings s = settings;
    s.fps = validFps(s.fps);

    if (!isProjectFile(projectPath)) {
        m_unsaved = s;
        emit changed();
        return;
    }

    const QString sidecar = sidecarPath(projectPath);
    if (!s.hasVideo()) {
        QFile::remove(sidecar);
        emit changed();
        return;
    }

    const QDir projectDir = QFileInfo(projectPath).absoluteDir();
    QJsonObject o;
    o["video"] = projectDir.relativeFilePath(s.file);
    o["videoAbsolute"] = QFileInfo(s.file).absoluteFilePath();
    o["offset"] = s.offset;
    o["fps"] = s.fps;

    QSaveFile f(sidecar);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
        f.commit();
    }
    emit changed();
}

void VideoStore::adopt(const QString& projectPath)
{
    if (!isProjectFile(projectPath) || !m_unsaved.hasVideo()) {
        return;
    }
    if (!QFileInfo::exists(sidecarPath(projectPath))) {
        set(projectPath, m_unsaved);
    }
    m_unsaved = VideoSettings();
}

void VideoStore::clearUnsaved()
{
    m_unsaved = VideoSettings();
}

void au::instruments::VideoStore::setCues(const std::vector<VideoCue>& cues)
{
    bool same = cues.size() == m_cues.size();
    for (size_t i = 0; same && i < cues.size(); ++i) {
        same = cues[i].t == m_cues[i].t && cues[i].v == m_cues[i].v;
    }
    if (same) {
        return;
    }
    m_cues = cues;
    emit changed();
}

double au::instruments::VideoStore::pictureAt(double t, double offset) const
{
    const VideoCue* last = nullptr;
    for (const VideoCue& c : m_cues) {
        if (c.t <= t + 1e-6) {
            last = &c;
        } else {
            break;
        }
    }
    if (!last) {
        return t - offset;
    }
    if (last->v < 0.0) {
        return -1.0;
    }
    return last->v + (t - last->t);
}
