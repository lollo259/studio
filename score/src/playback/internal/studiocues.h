/*
 * Score (Laurence Who Studio): playback that waits for a cue.
 * A staff or system text that starts with "wait" (e.g. "wait for cue") or "cadenza" stops
 * playback right there; it goes on from there with play (Space, Studio's play key), any key
 * on the MIDI keyboard or the sustain pedal.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <vector>

#include <QJsonArray>
#include <QTimer>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "midi/imidiinport.h"
#include "../iplaybackcontroller.h"
#include "project/iprojectsession.h"
#include "notation/imasternotation.h"

namespace mu::playback {
//! Video cues: staff/system texts "video <time>" ("video 1:30", "video 00:01:30:12", "video off");
//! [{t: seconds on the score timeline, v: picture time or -1 for no picture}], in playing order
QJsonArray studioVideoCues(const mu::notation::IMasterNotationPtr& master);

//! The tempo for Tape's tempo strip: [{t: seconds, bpm, tick}], a point where it changes
QJsonArray studioTempoPoints(const mu::notation::IMasterNotationPtr& master);
//! Sets the tempo at that tick (the marking there is changed, or one is added); one undo step
bool studioSetTempo(const mu::notation::IMasterNotationPtr& master, int tick, double bpm);

class StudioCues : public muse::Contextable, public muse::async::Asyncable
{
    muse::ContextInject<IPlaybackController> controller = { this };
    muse::ContextInject<IPlaybackState> state = { this };
    muse::ContextInject<project::IProjectSession> projectSession = { this };
    muse::GlobalInject<muse::midi::IMidiInPort> midiInPort;

public:
    explicit StudioCues(const muse::modularity::ContextPtr& ctx)
        : muse::Contextable(ctx) {}

    void init();

private:
    void collect();
    void collectFollow();
    void onPosition(double pos);
    void stopAtCue(double at);
    void resume();

    std::vector<double> m_waits; // seconds, in playing order
    std::vector<double> m_onsets; // Follow me: the followed instrument's notes, seconds
    double m_lastTrigger = -1.0;  // ms clock of the last note that moved playback (chords count once)
    bool m_dirty = true;
    bool m_waiting = false;
    double m_waitAt = -1.0;
    double m_resumedAt = -1.0;
    QTimer m_timer;
};
}
