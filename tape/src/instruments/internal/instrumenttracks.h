/*
 * Tape (Laurence Who Studio) — instruments
 * Instrument tracks: a track in the project that the virtual instrument records into.
 * Recording a take on an instrument track plays the project along (so you hear the
 * other tracks) and drops the take into that track exactly where it started.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <QString>

#include "framework/actions/iactionsdispatcher.h"
#include "framework/global/modularity/ioc.h"
#include "framework/interactive/iinteractive.h"
#include "context/iglobalcontext.h"
#include "playback/iplaybackcontroller.h"
#include "trackedit/iselectioncontroller.h"
#include "trackedit/itrackeditinteraction.h"

namespace au::instruments {
class InstrumentTracks : public muse::Contextable
{
    muse::ContextInject<au::context::IGlobalContext> globalContext{ this };
    muse::ContextInject<trackedit::ISelectionController> selectionController{ this };
    muse::ContextInject<trackedit::ITrackeditInteraction> trackeditInteraction{ this };
    muse::ContextInject<au::playback::IPlaybackController> playbackController{ this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher{ this };
    muse::ContextInject<muse::IInteractive> interactive{ this };

public:
    InstrumentTracks(const muse::modularity::ContextPtr& ctx)
        : muse::Contextable(ctx) {}

    static QString prefix();                 // "Instrument · "
    static bool isInstrumentTitle(const QString& title);

    //! adds a new instrument track named after the current sound and selects it
    void addInstrumentTrack();

    //! starts a take; returns false if the synth could not start
    bool startTake(const QString& folder, const QString& name);
    //! stops it and puts it in the project; returns the audio file
    QString stopTake();
    bool isTaking() const;

    //! opens the MIDI of the last take in Score (as notation)
    bool openLastTakeInScore(QString* error = nullptr);
    static QString findScore();
    QString lastMidi() const { return m_lastMidi; }

private:
    trackedit::TrackId targetTrack() const;

    trackedit::TrackId m_takeTrack = -1;
    double m_takeStart = 0.0;
    bool m_startedPlayback = false;
    QString m_lastMidi;
};
}
