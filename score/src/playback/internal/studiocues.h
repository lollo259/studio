/*
 * Score (Laurence Who Studio): playback that waits for a cue.
 * A staff or system text that starts with "wait" (e.g. "wait for cue") or "cadenza" stops
 * playback right there; it goes on from there with play (Space, Studio's play key), any key
 * on the MIDI keyboard or the sustain pedal.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <vector>

#include <QTimer>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "midi/imidiinport.h"
#include "../iplaybackcontroller.h"
#include "project/iprojectsession.h"

namespace mu::playback {
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
    void onPosition(double pos);
    void stopAtCue(double at);
    void resume();

    std::vector<double> m_waits; // seconds, in playing order
    bool m_dirty = true;
    bool m_waiting = false;
    double m_waitAt = -1.0;
    double m_resumedAt = -1.0;
    QTimer m_timer;
};
}
