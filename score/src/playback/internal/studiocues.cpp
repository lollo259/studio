/*
 * Score (Laurence Who Studio): playback that waits for a cue.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "studiocues.h"

#include <algorithm>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/repeatlist.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/textbase.h"
#include "engraving/dom/tempotext.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/factory.h"
#include "notation/inotationundostack.h"
#include <QRegularExpression>
#include <cmath>
#include "engraving/dom/part.h"
#include "engraving/dom/staff.h"
#include "notation/studiofollow.h"
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
    studioOnset() = [this]() { onPlayed(true); };
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
            if (m_waiting || studioCueWaiting()) {
                studioCueWaiting() = false;
                if (studioCueWaitingChanged()) {
                    studioCueWaitingChanged()(false);
                }
            }
            m_waiting = false;
            if (mu::notation::studio::followMeOn()) {
                collectFollow();
            } else {
                m_onsets.clear();
            }
            if (studioMicListen()) {
                studioMicListen()(!m_onsets.empty());
            }
        } else {
            m_timer.stop();
            if (studioMicListen() && !m_onsets.empty() && !m_waiting) {
                studioMicListen()(false);
            }
        }
    });
    state()->playbackPositionChanged().onReceive(this, [this](muse::audio::secs_t pos) {
        onPosition(double(pos));
    });

    midiInPort()->eventReceived().onReceive(this, [this](muse::midi::tick_t, const muse::midi::Event& e) {
        if (!e.isChannelVoice()) {
            return;
        }
        using Op = muse::midi::Event::Opcode;
        const bool key = e.opcode() == Op::NoteOn && e.velocity7() > 0;
        const bool pedal = e.opcode() == Op::ControlChange && e.index() == 64 && e.data() >= 64;
        if (!key && !pedal) {
            return;
        }
        onPlayed(key);
    });
}


// a note played (MIDI key, pedal, or a note heard on the mic): goes on from a wait, or - Follow me -
// jumps to your note when you play early (a chord counts once)
void StudioCues::onPlayed(bool key)
{
    if (m_waiting) {
        resume();
        return;
    }
    // Follow me: you played early - playback jumps to your note (a chord counts once)
    if (key && !m_onsets.empty() && state()->isPlaying()) {
        const double now = double(QDateTime::currentMSecsSinceEpoch());
        if (now - m_lastTrigger < 90.0) {
            return;
        }
        const double pos = state()->playbackPosition();
        for (double o : m_onsets) {
            if (o <= m_resumedAt + 0.02 || o < pos - 0.02) {
                continue;
            }
            if (o - pos <= 0.8) {
                m_lastTrigger = now;
                m_timer.stop();
                m_resumedAt = o;
                controller()->rewind(muse::secs_t(o));
            }
            break;
        }
    }
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
    if ((m_waits.empty() && m_onsets.empty()) || !state()->isPlaying() || m_timer.isActive()) {
        return;
    }
    double next = -1.0;
    for (const std::vector<double>* list : { &m_waits, &m_onsets }) {
        for (double w : *list) {
            if (w <= m_resumedAt + 0.02 || w < pos - 0.02) {
                continue;
            }
            if (next < 0.0 || w < next) {
                next = w;
            }
            break;
        }
    }
    if (next >= 0.0 && next - pos <= 0.25) {
        m_waitAt = next;
        m_timer.start(std::max(0, int((next - pos) * 1000.0) - 5));
    }
}

// Follow me: the notes of the instrument you play (the one selected, else the first)
void StudioCues::collectFollow()
{
    m_onsets.clear();
    auto master = projectSession()->masterNotation();
    MasterScore* score = master ? master->masterScore() : nullptr;
    if (!score || !master->playback() || score->parts().empty()) {
        return;
    }
    const Part* part = score->parts().front();
    if (const EngravingItem* sel = score->selection().element()) {
        if (sel->part()) {
            part = sel->part();
        }
    } else if (score->selection().isRange()) {
        const staff_idx_t si = score->selection().staffStart();
        if (si < score->nstaves()) {
            part = score->staff(si)->part();
        }
    }
    std::vector<int> ticks;
    for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
        const TrackRange range = part->trackRange();
        for (track_idx_t t = range.startTrack; t < range.endTrack; ++t) {
            const EngravingItem* e = s->element(t);
            if (e && e->isChord()) {
                ticks.push_back(s->tick().ticks());
                break;
            }
        }
    }
    for (const RepeatSegment* rs : score->repeatList()) {
        for (int t : ticks) {
            if (t >= rs->tick && t < rs->tick + rs->len()) {
                m_onsets.push_back(double(master->playback()->playedTickToSec(t + rs->utick - rs->tick)));
            }
        }
    }
    std::sort(m_onsets.begin(), m_onsets.end());
}

void StudioCues::stopAtCue(double at)
{
    if (!state()->isPlaying()) {
        return;
    }
    m_waiting = true;
    studioCueWaiting() = true;
    controller()->pause();
    controller()->rewind(muse::secs_t(at));
    if (studioCueWaitingChanged()) {
        studioCueWaitingChanged()(true);
    }
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

// Studio: the tempo as Tape draws it: a point wherever the tempo changes, in playing order
QJsonArray mu::playback::studioTempoPoints(const mu::notation::IMasterNotationPtr& master)
{
    QJsonArray out;
    MasterScore* score = master ? master->masterScore() : nullptr;
    if (!score || !master->playback()) {
        return out;
    }
    double last = -1.0;
    for (const RepeatSegment* rs : score->repeatList()) {
        for (const Measure* m = score->tick2measure(Fraction::fromTicks(rs->tick)); m && m->tick().ticks() < rs->tick + rs->len();
             m = m->nextMeasure()) {
            for (const Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
                const int tick = s->tick().ticks();
                const double bpm = score->tempo(s->tick()).val * 60.0;
                if (std::abs(bpm - last) < 0.05) {
                    continue;
                }
                last = bpm;
                const double t = double(master->playback()->playedTickToSec(tick + rs->utick - rs->tick));
                out.append(QJsonObject { { "t", t }, { "bpm", bpm }, { "tick", tick } });
            }
        }
    }
    return out;
}

// Studio: a tempo point dragged in Tape: change the tempo marking there (or add one)
bool mu::playback::studioSetTempo(const mu::notation::IMasterNotationPtr& master, int tick, double bpm)
{
    MasterScore* score = master ? master->masterScore() : nullptr;
    if (!score || bpm < 5.0 || bpm > 999.0) {
        return false;
    }
    Measure* measure = score->tick2measure(Fraction::fromTicks(tick));
    Segment* seg = measure ? measure->findSegment(SegmentType::ChordRest, Fraction::fromTicks(tick)) : nullptr;
    if (!seg && measure) {
        seg = measure->first(SegmentType::ChordRest);
    }
    if (!seg) {
        return false;
    }
    TempoText* found = nullptr;
    for (EngravingItem* a : seg->annotations()) {
        if (a->isTempoText()) {
            found = toTempoText(a);
            break;
        }
    }
    const QString number = QString::number(std::lround(bpm));
    master->notation()->undoStack()->prepareChanges(muse::TranslatableString::untranslatable("Tempo from Tape"));
    if (found) {
        const double old = found->tempo().val * 60.0;
        QString xml = found->xmlText().toQString();
        static const QRegularExpression re("=\\s*(\\d+(?:[.,]\\d+)?)");
        const QRegularExpressionMatch mt = re.match(xml);
        if (mt.hasMatch() && old > 0.0) {
            const double shown = mt.captured(1).replace(',', '.').toDouble();
            const QString now = QString::number(std::lround(shown * bpm / old));
            xml.replace(mt.capturedStart(1), mt.capturedLength(1), now);
            static_cast<EngravingObject*>(found)->undoChangeProperty(Pid::TEXT, PropertyValue(String::fromQString(xml)));
        }
        static_cast<EngravingObject*>(found)->undoChangeProperty(Pid::TEMPO, PropertyValue(BeatsPerSecond(bpm / 60.0)));
    } else {
        TempoText* tt = Factory::createTempoText(seg);
        tt->setTrack(0);
        tt->setXmlText(String::fromQString("<sym>metNoteQuarterUp</sym> = " + number));
        tt->setTempo(BeatsPerSecond(bpm / 60.0));
        tt->setFollowText(true);
        score->undoAddElement(tt);
    }
    master->notation()->undoStack()->commitChanges();
    master->notation()->notationChanged().send({});
    return true;
}

QJsonArray mu::playback::studioWaitTimes(const mu::notation::IMasterNotationPtr& master)
{
    QJsonArray out;
    MasterScore* score = master ? master->masterScore() : nullptr;
    if (!score || !master->playback()) {
        return out;
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
    std::vector<double> times;
    for (const RepeatSegment* rs : score->repeatList()) {
        for (int t : ticks) {
            if (t >= rs->tick && t < rs->tick + rs->len()) {
                times.push_back(double(master->playback()->playedTickToSec(t + rs->utick - rs->tick)));
            }
        }
    }
    std::sort(times.begin(), times.end());
    for (double t : times) {
        out.append(t);
    }
    return out;
}
