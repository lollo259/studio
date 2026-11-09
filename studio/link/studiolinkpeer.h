/*
 * Studio live link (Laurence Who Studio) — shared by Score and Tape.
 * Every app that opens joins one local channel. The first one hosts it, the others
 * connect; if the host closes, the next one takes over. Messages are small JSON
 * objects ({"cmd":"play","t":12.5}) passed to every other app.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

class QLocalServer;
class QLocalSocket;

namespace studiolink {
class Peer : public QObject
{
    Q_OBJECT
public:
    explicit Peer(const QString& appName, QObject* parent = nullptr);
    ~Peer() override;

    void start();
    void send(QJsonObject msg);
    bool isLinked() const;
    QString appName() const { return m_app; }

signals:
    void received(const QJsonObject& msg);
    void linkedChanged(bool linked);

private:
    void tryJoin();
    void attach(QLocalSocket* s);
    void readFrom(QLocalSocket* s);
    void relay(const QByteArray& line, QLocalSocket* except);
    void updateLinked();

    QString m_app;
    QLocalServer* m_server = nullptr;
    QLocalSocket* m_client = nullptr;   // when another app hosts
    QList<QLocalSocket*> m_peers;        // when we host
    QTimer m_retry;
    bool m_linked = false;
};
}
