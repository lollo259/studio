/*
 * Score (Laurence Who Studio): playback that waits for a cue.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "studiocues.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/repeatlist.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/textbase.h"
#include "notation/imasternotation.h"
#include "notation/inotation.h"
#include "notation/inotationplayback.h"

using namespace mu::playback;
using namespace mu::engraving;

static bool isCueText(const EngravingItem* a)
{
    if (!a || !(a->isStaffText() || a->isSystemText())) {
        return false;
    }
    const QString t = toTextBase(a)->plainText().toQString().trimmed().toLower();
    return t.startsWith("wait") || t.startsWith("cadenza") || t.startsWith("cadenz");
}

void StudioCues::init()
{
    m_timer.setSingleShot(true);
    QObject::connect(&m_timer, &QTimer::timeout, [this]() {
        if (m_waitAt >= 0.0) {
            stopAtCue(m_waitAt);
        }
    });

    auto watchScore = [this]() {
        m_dirty = true;
        auto master = projectSession()->masterNotation();
        if (master && master->notation()) {
            master->notation()->notationChanged().onReceive(this, [this](const muse::RectF&) { m_dirty = true; });
        }
    };
    projectSession()->masterNotationChanged().onNotify(this, watchScore);
    watchScore();

    state()->isPlayingChanged().onReceive(this, [this](bool playing) {
        if (playing) {
            if (m_dirty) {
                collect();
            }
            m_resumedAt = state()->playbackPosition();
            m_waiting = false;
        } else {
            m_timer.stop();
        }
    });
    state()->playbackPositionChanged().onReceive(this, [this](muse::audio::secs_t pos) {
        onPosition(double(pos));
    });

    midiInPort()->eventReceived().onReceive(this, [this](muse::midi::tick_t, const muse::midi::Event& e) {
        if (!m_waiting || !e.isChannelVoice()) {
            return;
        }
        using Op = muse::midi::Event::Opcode;
        const bool key = e.opcode() == Op::NoteOn && e.velocity7() > 0;
        const bool pedal = e.opcode() == Op::ControlChange && e.index() == 64 && e.data() >= 64;
        if (key || pedal) {
            resume();
        }
    });
}

void StudioCues::collect()
{
    m_dirty = false;
    m_waits.clear();
    auto master = projectSession()->masterNotation();
    MasterScore* score = master ? master->masterScore() : nullptr;
    if (!score || !master->playback()) {
        return;
    }
    std::vector<int> ticks;
    for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
        for (EngravingItem* a : s->annotations()) {
            if (isCueText(a)) {
                ticks.push_back(s->tick().ticks());
                break;
            }
        }
    }
    if (ticks.empty()) {
        return;
    }
    for (const RepeatSegment* rs : score->repeatList()) {
        for (int t : ticks) {
            if (t >= rs->tick && t < rs->tick + rs->len()) {
                const int utick = t + rs->utick - rs->tick;
                m_waits.push_back(double(master->playback()->playedTickToSec(utick)));
            }
        }
    }
    std::sort(m_waits.begin(), m_waits.end());
}

void StudioCues::onPosition(double pos)
{
    if (m_waits.empty() || !state()->isPlaying() || m_timer.isActive()) {
        return;
    }
    for (double w : m_waits) {
        if (w <= m_resumedAt + 0.02 || w < pos - 0.02) {
            continue;
        }
        if (w - pos <= 0.25) {
            m_waitAt = w;
            m_timer.start(std::max(0, int((w - pos) * 1000.0) - 5));
        }
        break;
    }
}

void StudioCues::stopAtCue(double at)
{
    if (!state()->isPlaying()) {
        return;
    }
    controller()->pause();
    controller()->rewind(muse::secs_t(at));
    m_waiting = true;
}

void StudioCues::resume()
{
    m_waiting = false;
    controller()->play(false);
}

// "video 1:30", "video 00:01:30:12", "video 90", "video off": from here the picture is at that time
static bool parseVideoCue(const QString& text, double* picture)
{
    QString t = text.trimmed().toLower();
    if (!t.startsWith("video")) {
        return false;
    }
    t = t.mid(5).trimmed();
    if (t.startsWith(':')) {
        t = t.mid(1).trimmed();
    }
    if (t.isEmpty()) {
        return false;
    }
    if (t == "off" || t == "black" || t == "stop") {
        *picture = -1.0;
        return true;
    }
    const QStringList p = t.section(' ', 0, 0).split(':');
    bool ok = true;
    bool ok2 = true;
    bool ok3 = true;
    bool ok4 = true;
    double secs = 0.0;
    if (p.size() == 1) {
        secs = p[0].toDouble(&ok);
    } else if (p.size() == 2) {
        secs = p[0].toInt(&ok) * 60.0 + p[1].toDouble(&ok2);
    } else if (p.size() == 3) {
        secs = p[0].toInt(&ok) * 3600.0 + p[1].toInt(&ok2) * 60.0 + p[2].toDouble(&ok3);
    } else if (p.size() == 4) {
        secs = p[0].toInt(&ok) * 3600.0 + p[1].toInt(&ok2) * 60.0 + p[2].toInt(&ok3) + p[3].toInt(&ok4) / 25.0;
    } else {
        ok = false;
    }
    if (!(ok && ok2 && ok3 && ok4) || secs < 0) {
        return false;
    }
    *picture = secs;
    return true;
}

QJsonArray mu::playback::studioVideoCues(const mu::notation::IMasterNotationPtr& master)
{
    QJsonArray out;
    MasterScore* score = master ? master->masterScore() : nullptr;
    if (!score || !master->playback()) {
        return out;
    }
    std::vector<std::pair<int, double> > cues;
    for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
        for (EngravingItem* a : s->annotations()) {
            double pic = 0.0;
            if ((a->isStaffText() || a->isSystemText()) && parseVideoCue(toTextBase(a)->plainText().toQString(), &pic)) {
                cues.push_back({ s->tick().ticks(), pic });
                break;
            }
        }
    }
    std::vector<std::pair<double, double> > timed;
    for (const RepeatSegment* rs : score->repeatList()) {
        for (const auto& [t, pic] : cues) {
            if (t >= rs->tick && t < rs->tick + rs->len()) {
                timed.push_back({ double(master->playback()->playedTickToSec(t + rs->utick - rs->tick)), pic });
            }
        }
    }
    std::sort(timed.begin(), timed.end());
    for (const auto& [t, pic] : timed) {
        out.append(QJsonObject { { "t", t }, { "v", pic } });
    }
    return out;
}
