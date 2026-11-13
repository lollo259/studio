/*
 * Tape (Laurence Who Studio) — the tempo strip under the timeline: the score's tempo changes,
 * drawn on Tape's time; drag a point up or down to change the tempo in Score.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "instruments/internal/tempostore.h"

namespace au::instruments {
class TempoStripModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVariantList points READ points NOTIFY pointsChanged)
    Q_PROPERTY(bool hasTempo READ hasTempo NOTIFY pointsChanged)

public:
    explicit TempoStripModel(QObject* parent = nullptr)
        : QObject(parent)
    {
        connect(TempoStore::instance(), &TempoStore::changed, this, &TempoStripModel::pointsChanged);
    }

    QVariantList points() const { return TempoStore::instance()->points(); }
    bool hasTempo() const { return !TempoStore::instance()->points().isEmpty(); }

    //! the point at that tick now has this tempo: Score changes its tempo marking
    Q_INVOKABLE void setTempo(int tick, double bpm)
    {
        QLocalSocket sock;
        sock.connectToServer("laurencewho-studio-link");
        if (!sock.waitForConnected(300)) {
            return;
        }
        const QJsonObject msg { { "cmd", "settempo" }, { "tick", tick }, { "bpm", bpm }, { "from", "tape-tempo" } };
        sock.write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + "\n");
        sock.flush();
        sock.waitForBytesWritten(300);
        sock.disconnectFromServer();
    }

signals:
    void pointsChanged();
};
}
