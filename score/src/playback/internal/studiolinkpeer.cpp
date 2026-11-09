/*
 * Studio live link (Laurence Who Studio)
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "studiolinkpeer.h"

#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>

using namespace studiolink;

static const char* CHANNEL = "laurencewho-studio-link";

Peer::Peer(const QString& appName, QObject* parent)
    : QObject(parent), m_app(appName)
{
    m_retry.setInterval(2000);
    connect(&m_retry, &QTimer::timeout, this, &Peer::tryJoin);
}

Peer::~Peer()
{
    m_retry.stop();
    if (m_client) {
        m_client->disconnect(this);
        m_client->abort();
    }
    for (QLocalSocket* s : m_peers) {
        s->disconnect(this);
        s->abort();
    }
}

void Peer::start()
{
    tryJoin();
    m_retry.start();
}

bool Peer::isLinked() const
{
    return m_linked;
}

void Peer::updateLinked()
{
    const bool now = (m_client && m_client->state() == QLocalSocket::ConnectedState) || !m_peers.isEmpty();
    if (now != m_linked) {
        m_linked = now;
        emit linkedChanged(now);
    }
}

void Peer::tryJoin()
{
    if (m_server || (m_client && m_client->state() == QLocalSocket::ConnectedState)) {
        return;
    }
    // somebody hosting already?
    auto* s = new QLocalSocket(this);
    s->connectToServer(CHANNEL);
    if (s->waitForConnected(150)) {
        m_client = s;
        connect(s, &QLocalSocket::readyRead, this, [this, s]() { readFrom(s); });
        connect(s, &QLocalSocket::disconnected, this, [this, s]() {
            if (m_client == s) {
                m_client = nullptr;
            }
            s->deleteLater();
            updateLinked();
            tryJoin(); // the host left: take over or join the new host
        });
        updateLinked();
        send(QJsonObject { { "cmd", "hello" } });
        return;
    }
    s->deleteLater();

    // nobody: host it
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(CHANNEL)) {
        QLocalServer::removeServer(CHANNEL); // a crashed host can leave it behind
        if (!m_server->listen(CHANNEL)) {
            delete m_server;
            m_server = nullptr;
            return;
        }
    }
    connect(m_server, &QLocalServer::newConnection, this, [this]() {
        while (QLocalSocket* c = m_server->nextPendingConnection()) {
            attach(c);
        }
    });
}

void Peer::attach(QLocalSocket* s)
{
    m_peers << s;
    connect(s, &QLocalSocket::readyRead, this, [this, s]() { readFrom(s); });
    connect(s, &QLocalSocket::disconnected, this, [this, s]() {
        m_peers.removeAll(s);
        s->deleteLater();
        updateLinked();
    });
    updateLinked();
}

void Peer::readFrom(QLocalSocket* s)
{
    while (s->canReadLine()) {
        const QByteArray line = s->readLine();
        if (m_server) {
            relay(line, s); // pass it on to the other apps
        }
        const QJsonObject msg = QJsonDocument::fromJson(line).object();
        if (!msg.isEmpty() && msg.value("from").toString() != m_app) {
            emit received(msg);
        }
    }
}

void Peer::relay(const QByteArray& line, QLocalSocket* except)
{
    for (QLocalSocket* p : m_peers) {
        if (p != except) {
            p->write(line);
            p->flush();
        }
    }
}

void Peer::send(QJsonObject msg)
{
    msg.insert("from", m_app);
    QByteArray line = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    line.append('\n');
    if (m_server) {
        relay(line, nullptr);
    } else if (m_client && m_client->state() == QLocalSocket::ConnectedState) {
        m_client->write(line);
        m_client->flush();
    }
}
