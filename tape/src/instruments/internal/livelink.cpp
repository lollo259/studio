/*
 * Tape (Laurence Who Studio) — Live (OBS) link
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "livelink.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>

using namespace au::instruments;

static const char* SOURCE = "Studio video";

LiveLink* LiveLink::instance()
{
    static LiveLink* s = new LiveLink();
    return s;
}

LiveLink::LiveLink()
{
    connect(&m_ws, &QWebSocket::textMessageReceived, this, &LiveLink::onText);
    connect(&m_ws, &QWebSocket::disconnected, this, [this]() {
        const bool was = m_identified;
        m_identified = false;
        m_pictureMade = false;
        if (was) {
            emit changed();
        }
    });
    m_retry.setInterval(1500);
    connect(&m_retry, &QTimer::timeout, this, [this]() {
        if (m_identified) {
            m_retry.stop();
            return;
        }
        connectSocket();
    });
}

static QString obsExe()
{
    // Studio folder: Studio/Tape/bin/Tape.exe and Studio/Live/bin/64bit/obs64.exe
    const QDir bin(QCoreApplication::applicationDirPath());
    for (const QString& c : { QString("../../Live/bin/64bit/obs64.exe"), QString("../Live/bin/64bit/obs64.exe") }) {
        const QString p = bin.absoluteFilePath(c);
        if (QFileInfo::exists(p)) {
            return QFileInfo(p).absoluteFilePath();
        }
    }
    return QString();
}

bool LiveLink::openLive(QString* error)
{
    if (m_identified) {
        return true;
    }
    const QString exe = obsExe();
    if (exe.isEmpty()) {
        if (error) {
            *error = "Live isn't installed with this Studio.";
        }
        connectSocket(); // an OBS started by hand works too
        m_retry.start();
        return false;
    }
    // a second OBS would only complain: start it only if nothing answers
    connectSocket();
    QTimer::singleShot(700, this, [this, exe]() {
        if (!m_identified) {
            QProcess::startDetached(exe, { "--portable", "--disable-shutdown-check" }, QFileInfo(exe).absolutePath());
        }
        m_retry.start();
    });
    return true;
}

void LiveLink::connectSocket()
{
    if (m_ws.state() == QAbstractSocket::ConnectedState || m_ws.state() == QAbstractSocket::ConnectingState) {
        return;
    }
    m_ws.open(QUrl("ws://127.0.0.1:4455"));
}

void LiveLink::request(const QString& type, const QJsonObject& data)
{
    if (!m_identified) {
        return;
    }
    QJsonObject d { { "requestType", type }, { "requestId", QString::number(m_nextId++) } };
    if (!data.isEmpty()) {
        d["requestData"] = data;
    }
    m_ws.sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject { { "op", 6 }, { "d", d } }).toJson(QJsonDocument::Compact)));
}

void LiveLink::refresh()
{
    request("GetSceneList");
    request("GetStreamStatus");
    request("GetRecordStatus");
}

void LiveLink::onText(const QString& text)
{
    const QJsonObject msg = QJsonDocument::fromJson(text.toUtf8()).object();
    const int op = msg.value("op").toInt(-1);
    const QJsonObject d = msg.value("d").toObject();
    if (op == 0) { // Hello
        QJsonObject identify { { "rpcVersion", 1 }, { "eventSubscriptions", 1 | 4 | 64 } }; // general, scenes, outputs
        if (d.contains("authentication")) {
            // Studio's Live runs without a password; an OBS with one set can't be driven from here
            const QJsonObject a = d.value("authentication").toObject();
            const QByteArray password = qEnvironmentVariable("STUDIO_LIVE_PASSWORD").toUtf8();
            const QByteArray secret = QCryptographicHash::hash(password + a.value("salt").toString().toUtf8(), QCryptographicHash::Sha256).toBase64();
            identify["authentication"] = QString::fromUtf8(
                QCryptographicHash::hash(secret + a.value("challenge").toString().toUtf8(), QCryptographicHash::Sha256).toBase64());
        }
        m_ws.sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject { { "op", 1 }, { "d", identify } }).toJson(QJsonDocument::Compact)));
    } else if (op == 2) { // Identified
        m_identified = true;
        m_retry.stop();
        refresh();
        if (!m_pictureFile.isEmpty()) {
            setPicture(m_pictureFile);
        }
        emit changed();
    } else if (op == 5) { // events
        const QString type = d.value("eventType").toString();
        const QJsonObject ed = d.value("eventData").toObject();
        if (type == "StreamStateChanged") {
            m_streaming = ed.value("outputActive").toBool();
            emit changed();
        } else if (type == "RecordStateChanged") {
            m_recording = ed.value("outputActive").toBool();
            emit changed();
        } else if (type == "CurrentProgramSceneChanged") {
            m_currentScene = ed.value("sceneName").toString();
            m_pictureMade = false;
            if (!m_pictureFile.isEmpty()) {
                setPicture(m_pictureFile);
            }
            emit changed();
        } else if (type.startsWith("Scene")) {
            request("GetSceneList");
        }
    } else if (op == 7) { // request responses
        const QString type = d.value("requestType").toString();
        const QJsonObject rd = d.value("responseData").toObject();
        if (type == "GetSceneList") {
            m_scenes.clear();
            const QJsonArray arr = rd.value("scenes").toArray();
            for (int i = arr.size() - 1; i >= 0; --i) { // OBS lists them bottom-up
                m_scenes << arr.at(i).toObject().value("sceneName").toString();
            }
            m_currentScene = rd.value("currentProgramSceneName").toString();
            emit changed();
        } else if (type == "GetStreamStatus") {
            m_streaming = rd.value("outputActive").toBool();
            emit changed();
        } else if (type == "GetRecordStatus") {
            m_recording = rd.value("outputActive").toBool();
            emit changed();
        } else if (type == "CreateInput" && !d.value("requestStatus").toObject().value("result").toBool()) {
            // it already exists (in another scene too): just point it at the film
            request("SetInputSettings", QJsonObject { { "inputName", SOURCE },
                                                      { "inputSettings", QJsonObject { { "local_file", m_pictureFile }, { "is_local_file", true } } } });
            request("CreateSceneItem", QJsonObject { { "sceneName", m_currentScene }, { "sourceName", SOURCE } });
        }
    }
}

void LiveLink::toggleStream()
{
    request("ToggleStream");
}

void LiveLink::toggleRecord()
{
    request("ToggleRecord");
}

void LiveLink::setScene(const QString& name)
{
    request("SetCurrentProgramScene", QJsonObject { { "sceneName", name } });
}

void LiveLink::setPicture(const QString& file)
{
    m_pictureFile = QDir::toNativeSeparators(file);
    if (!m_identified || m_pictureFile.isEmpty() || m_currentScene.isEmpty() || m_pictureMade) {
        return;
    }
    m_pictureMade = true;
    const QJsonObject settings { { "local_file", m_pictureFile }, { "is_local_file", true }, { "restart_on_activate", false },
                                 { "close_when_inactive", false }, { "clear_on_media_end", false } };
    request("CreateInput", QJsonObject { { "sceneName", m_currentScene }, { "inputName", SOURCE }, { "inputKind", "ffmpeg_source" },
                                         { "inputSettings", settings }, { "sceneItemEnabled", true } });
}

void LiveLink::syncPicture(double pictureSecs, bool playing)
{
    if (!m_identified || m_pictureFile.isEmpty()) {
        return;
    }
    if (playing != m_lastPlaying || !playing) {
        request("SetMediaInputCursor", QJsonObject { { "inputName", SOURCE }, { "mediaCursor", std::max(0.0, pictureSecs * 1000.0) } });
        request("TriggerMediaInputAction", QJsonObject { { "inputName", SOURCE },
                                                         { "mediaAction", playing ? "OBS_WEBSOCKET_MEDIA_INPUT_ACTION_PLAY"
                                                           : "OBS_WEBSOCKET_MEDIA_INPUT_ACTION_PAUSE" } });
    }
    m_lastPlaying = playing;
}
