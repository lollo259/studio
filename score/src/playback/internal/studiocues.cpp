/*
 * Score (Laurence Who Studio): playback that waits for a cue.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "studiocues.h"

#include <algorithm>

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
