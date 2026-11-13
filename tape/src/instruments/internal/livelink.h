/*
 * Tape (Laurence Who Studio) — Live: Studio's copy of OBS Studio, driven from Tape's video window
 * over obs-websocket (v5, localhost:4455). Open Live, stream / record, switch scenes, and put the
 * project's film in Live as a "Studio video" source that plays, pauses and seeks with Tape.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QWebSocket>

namespace au::instruments {
class LiveLink : public QObject
{
    Q_OBJECT
public:
    static LiveLink* instance();

    bool connected() const { return m_identified; }
    bool streaming() const { return m_streaming; }
    bool recording() const { return m_recording; }
    QStringList scenes() const { return m_scenes; }
    QString currentScene() const { return m_currentScene; }

    //! starts Live (OBS) if it isn't running, then connects
    bool openLive(QString* error = nullptr);
    void toggleStream();
    void toggleRecord();
    void setScene(const QString& name);

    //! the film in Live: a "Studio video" media source in the current scene
    void setPicture(const QString& file);
    //! keep it with Tape: playing from that point of the film, or paused there
    void syncPicture(double pictureSecs, bool playing);

signals:
    void changed();

private:
    LiveLink();
    void connectSocket();
    void onText(const QString& text);
    void request(const QString& type, const QJsonObject& data = {});
    void refresh();

    QWebSocket m_ws;
    QTimer m_retry;
    bool m_identified = false;
    bool m_streaming = false;
    bool m_recording = false;
    QStringList m_scenes;
    QString m_currentScene;
    QString m_pictureFile;
    bool m_pictureMade = false;
    bool m_lastPlaying = false;
    int m_nextId = 1;
};
}
