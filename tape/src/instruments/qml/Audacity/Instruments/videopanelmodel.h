/*
 * Tape (Laurence Who Studio) — video
 * Model behind the Video window: plays the project's film locked to Tape's playhead.
 *  - Tape plays   -> the film plays from the playhead (drift over 40 ms is corrected by a reseek)
 *  - Tape stops   -> the film pauses on the playhead's frame
 *  - the playhead moves while stopped (scrubbing, a click on the timeline, a seek from Score) -> the film follows
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <QElapsedTimer>
#include <QMediaPlayer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "framework/actions/iactionsdispatcher.h"
#include "framework/global/async/asyncable.h"
#include "framework/global/modularity/ioc.h"
#include "framework/interactive/iinteractive.h"
#include "context/iglobalcontext.h"

#include "instruments/internal/videostore.h"

namespace au::instruments {
class VideoPanelModel : public QObject, public muse::async::Asyncable, public muse::Contextable
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject * videoOutput READ videoOutput WRITE setVideoOutput NOTIFY videoOutputChanged)
    Q_PROPERTY(bool hasProject READ hasProject NOTIFY videoChanged)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY videoChanged)
    Q_PROPERTY(QString videoName READ videoName NOTIFY videoChanged)
    Q_PROPERTY(QString videoFile READ videoFile NOTIFY videoChanged)
    Q_PROPERTY(QString videoInfo READ videoInfo NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY statusChanged)
    Q_PROPERTY(double offset READ offset WRITE setOffset NOTIFY settingsChanged)
    Q_PROPERTY(QString offsetText READ offsetText NOTIFY settingsChanged)
    Q_PROPERTY(double fps READ fps WRITE setFps NOTIFY settingsChanged)
    Q_PROPERTY(int fpsIndex READ fpsIndex WRITE setFpsIndex NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList fpsOptions READ fpsOptions CONSTANT)
    Q_PROPERTY(QString timecode READ timecode NOTIFY timeChanged)
    Q_PROPERTY(QString tapeTime READ tapeTime NOTIFY timeChanged)
    Q_PROPERTY(QString pictureState READ pictureState NOTIFY timeChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY timeChanged)

    muse::ContextInject<au::context::IGlobalContext> globalContext{ this };
    muse::ContextInject<muse::IInteractive> interactive{ this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher{ this };

public:
    explicit VideoPanelModel(QObject* parent = nullptr);
    ~VideoPanelModel() override;

    Q_INVOKABLE void load();
    Q_INVOKABLE void chooseVideo();
    Q_INVOKABLE void clearVideo();
    Q_INVOKABLE void importSound();
    Q_INVOKABLE void offsetToPlayhead();
    Q_INVOKABLE bool setOffsetText(const QString& text);
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void restoreWindow(QObject* window);
    Q_INVOKABLE void saveWindow(QObject* window);

    QObject* videoOutput() const;
    void setVideoOutput(QObject* output);

    bool hasProject() const;
    bool hasVideo() const;
    QString videoName() const;
    QString videoFile() const;
    QString videoInfo() const;
    QString error() const;
    double offset() const;
    void setOffset(double secs);
    QString offsetText() const;
    double fps() const;
    void setFps(double fps);
    int fpsIndex() const;
    void setFpsIndex(int index);
    QVariantList fpsOptions() const;
    QString timecode() const;
    QString tapeTime() const;
    QString pictureState() const;
    bool playing() const;

    //! HH:MM:SS:FF (29.97 is counted non-drop); negative times get a leading minus
    static QString formatTimecode(double secs, double fps);
    //! "HH:MM:SS:FF", "MM:SS:FF", "SS:FF" or plain seconds ("12.5")
    static bool parseTimecode(const QString& text, double fps, double* secs);

signals:
    void videoOutputChanged();
    void videoChanged();
    void settingsChanged();
    void statusChanged();
    void timeChanged();

private:
    void subscribe();
    void reloadFromStore();
    void store();
    void openSource();
    void sync(bool force);
    double tapePosition() const;
    bool tapeRunning() const;
    QString projectPath() const;

    QMediaPlayer m_player;
    QPointer<QObject> m_output;
    VideoSettings m_settings;
    QString m_loadedFile;
    QString m_error;

    QTimer m_tick;       // while Tape plays: drift check + timecode
    QTimer m_seekTimer;  // while stopped: coalesces scrubbing into at most one seek per frame
    QElapsedTimer m_lastReseek;
    bool m_storing = false;
};
}
