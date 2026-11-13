/*
 * Tape (Laurence Who Studio) — the score's tempo, as Score sends it, for the tempo strip
 * under the timeline. Dragging a point there sends the new tempo back to Score.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <QObject>
#include <QVariantList>

namespace au::instruments {
class TempoStore : public QObject
{
    Q_OBJECT
public:
    static TempoStore* instance()
    {
        static TempoStore s;
        return &s;
    }

    //! [{t: seconds, bpm, tick}]
    const QVariantList& points() const { return m_points; }
    void setPoints(const QVariantList& points)
    {
        if (points == m_points) {
            return;
        }
        m_points = points;
        emit changed();
    }

signals:
    void changed();

private:
    TempoStore() = default;
    QVariantList m_points;
};
}
