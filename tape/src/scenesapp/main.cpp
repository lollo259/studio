/*
 * Scenes (Laurence Who Studio) — a live sound switcher
 * Scenes.exe [set.scenes.json]
 * One Scenes at a time: starting it again (e.g. Tape's Export to Scenes) hands the
 * file to the one already running.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSettings>
#include <QTimer>

#include "mainwindow.h"
#include "scenesformat.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

int main(int argc, char** argv)
{
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    QApplication::setStyle("Fusion"); // built into Qt Widgets: the same base look everywhere
    QApplication::setApplicationName("Scenes");
    QApplication::setOrganizationName("LaurenceWho");
    QApplication::setWindowIcon(QIcon(":/scenes/scenes.png"));

    QString file;
    bool perform = false;   // --perform: start full screen
    QString screenshot; // --screenshot out.png: a picture of the window, then quit (checks)
    const QStringList args = QApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--perform") {
            perform = true;
        } else if (args[i] == "--screenshot" && i + 1 < args.size()) {
            screenshot = args[++i];
        } else if (!args[i].startsWith("-")) {
            file = QFileInfo(args[i]).absoluteFilePath();
        }
    }

    // already running: give it the file and step aside
    const QString serverName = "laurencewho-scenes-" + qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER"));
    if (screenshot.isEmpty()) {
        QLocalSocket sock;
        sock.connectToServer(serverName);
        if (sock.waitForConnected(300)) {
            sock.write((file.isEmpty() ? QString("show") : file).toUtf8() + "\n");
            sock.flush();
            sock.waitForBytesWritten(500);
            return 0;
        }
    }

#ifdef _WIN32
    // the audio callback and MIDI should never wait for the UI
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#endif

    for (const char* f : { ":/scenes/fonts/Jost-Regular.ttf", ":/scenes/fonts/JostStar-Light.ttf" }) {
        QFontDatabase::addApplicationFont(f);
    }

    scenes::MainWindow w;

    QLocalServer::removeServer(serverName);
    QLocalServer server;
    server.listen(serverName);
    QObject::connect(&server, &QLocalServer::newConnection, &w, [&]() {
        while (QLocalSocket* s = server.nextPendingConnection()) {
            QObject::connect(s, &QLocalSocket::readyRead, &w, [s, &w]() {
                if (!s->canReadLine()) {
                    return;
                }
                const QString line = QString::fromUtf8(s->readLine()).trimmed();
                if (!line.isEmpty() && line != "show") {
                    w.openSet(line);
                }
                w.showNormal();
                w.raise();
                w.activateWindow();
                s->disconnectFromServer();
            });
            QObject::connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
        }
    });

    QString open = file;
    if (open.isEmpty()) {
        open = QSettings("LaurenceWho", "Scenes").value("lastSet").toString();
    }
    if (open.isEmpty() || !QFileInfo::exists(open)) {
        // first time: a small starter set in Documents/Studio/Scenes
        open = scenes::setPath("My set");
        if (!QFileInfo::exists(open)) {
            scenes::saveSet(scenes::defaultSet(), open);
        }
    }
    w.openSet(open);
    w.show();
    if (perform) {
        w.togglePerform();
    }
    if (!screenshot.isEmpty()) {
        QTimer::singleShot(2500, &w, [&w, screenshot]() {
            (w.performView() ? w.performView() : static_cast<QWidget*>(&w))->grab().save(screenshot);
            QApplication::quit();
        });
    }
    return app.exec();
}
