/*
 * Tape (Laurence Who Studio) — video
 * Where the picture of a project is remembered: a sidecar file next to the project,
 * "<project>.studio-video.json", holding the video file, its offset on the timeline and the frame rate.
 * A project that has not been saved yet keeps it in memory until it gets a file.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <QObject>
#include <QString>

#include <vector>

namespace au::instruments {
struct VideoSettings
{
    QString file;          // absolute path of the video
    double offset = 0.0;   // seconds: where the first frame of the video sits on Tape's timeline
    double fps = 25.0;     // frame rate of the timecode display: 24, 25, 29.97 or 30

    bool hasVideo() const { return !file.isEmpty(); }
};

//! Studio: video cues from the score ("video 1:30" texts): at score time t the picture is at v
//! (v < 0: no picture); the picture runs on from there
struct VideoCue
{
    double t = 0.0;
    double v = 0.0;
};

class VideoStore : public QObject
{
    Q_OBJECT

public:
    static VideoStore* instance();

    //! projectPath: the project file (.aup4); anything else counts as "not saved yet"
    VideoSettings get(const QString& projectPath) const;
    void set(const QString& projectPath, const VideoSettings& settings);

    //! An unsaved project just got a file: write what it had in memory next to it
    void adopt(const QString& projectPath);
    void clearUnsaved();

    static QString sidecarPath(const QString& projectPath);

    void setCues(const std::vector<VideoCue>& cues);
    const std::vector<VideoCue>& cues() const { return m_cues; }
    //! where the picture is at Tape time t: with cues, the cue map; without, t - offset
    double pictureAt(double t, double offset) const;
    static bool isProjectFile(const QString& projectPath);

signals:
    void changed();

private:
    VideoStore() = default;

    VideoSettings m_unsaved;
    std::vector<VideoCue> m_cues;
};
}
