/*
 * Tape (Laurence Who Studio) — instruments
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "instrumentsactions.h"

#include "framework/global/types/translatablestring.h"
#include "context/uicontext.h"
#include "context/shortcutcontext.h"

#include "../instrumentsmodule.h"
#include "instrumentsservice.h"

using namespace au::instruments;
using namespace muse;
using namespace muse::ui;
using namespace muse::actions;

static const ActionCode OPEN_CODE("instruments");
static const ActionCode SAMPLER_CODE("sampler");
static const ActionCode PANIC_CODE("instruments-panic");
static const ActionCode NEW_TRACK_CODE("instruments-new-track");
static const ActionCode OPEN_IN_SCORE_CODE("instruments-open-in-score");
static const ActionCode EXPORT_SCENES_CODE("instruments-export-scenes");
static const ActionCode COUNT_IN_1_CODE("record-count-in-1");
static const ActionCode COUNT_IN_2_CODE("record-count-in-2");

static const UiActionList s_actions = {
    UiAction(OPEN_CODE,
             au::context::UiCtxAny,
             au::context::CTX_ANY,
             TranslatableString("action", "&Instruments…"),
             TranslatableString("action_description", "Play virtual instruments from your MIDI keyboard")),
    UiAction(SAMPLER_CODE,
             au::context::UiCtxAny,
             au::context::CTX_ANY,
             TranslatableString("action", "&Sampler…"),
             TranslatableString("action_description", "16 pads: load sounds, trim them and play them from the pads or your keyboard")),
    UiAction(PANIC_CODE,
             au::context::UiCtxAny,
             au::context::CTX_ANY,
             TranslatableString("action", "All notes off"),
             TranslatableString("action_description", "Stop every sounding note of the instruments")),
    UiAction(NEW_TRACK_CODE,
             au::context::UiCtxAny,
             au::context::CTX_ANY,
             TranslatableString("action", "New &instrument track"),
             TranslatableString("action_description", "Add a track played by a virtual instrument from your MIDI keyboard")),
    UiAction(OPEN_IN_SCORE_CODE,
             au::context::UiCtxAny,
             au::context::CTX_ANY,
             TranslatableString("action", "Open last take in &Score"),
             TranslatableString("action_description", "Open the notes of the last instrument take in Score")),
    UiAction(COUNT_IN_1_CODE,
             au::context::UiCtxAny,
             au::context::CTX_ANY,
             TranslatableString("action", "Record with count-in (1 bar)"),
             TranslatableString("action_description", "Click one bar at the project's tempo and metre, then record on the current track")),
    UiAction(COUNT_IN_2_CODE,
             au::context::UiCtxAny,
             au::context::CTX_ANY,
             TranslatableString("action", "Record with count-in (2 bars)"),
             TranslatableString("action_description", "Click two bars at the project's tempo and metre, then record on the current track")),
};

void InstrumentsActions::init()
{
    dispatcher()->reg(this, OPEN_CODE, this, &InstrumentsActions::openPanel);
    dispatcher()->reg(this, SAMPLER_CODE, this, &InstrumentsActions::openSampler);
    dispatcher()->reg(this, PANIC_CODE, this, &InstrumentsActions::panic);
    dispatcher()->reg(this, NEW_TRACK_CODE, this, &InstrumentsActions::newInstrumentTrack);
    dispatcher()->reg(this, OPEN_IN_SCORE_CODE, this, &InstrumentsActions::openInScore);
    dispatcher()->reg(this, EXPORT_SCENES_CODE, this, &InstrumentsActions::exportToScenes);
    dispatcher()->reg(this, COUNT_IN_1_CODE, [this]() { recordWithCountIn(1); });
    dispatcher()->reg(this, COUNT_IN_2_CODE, [this]() { recordWithCountIn(2); });
}

InstrumentTracks& InstrumentsActions::tracks()
{
    if (!m_tracks) {
        m_tracks = std::make_unique<InstrumentTracks>(iocContext());
    }
    return *m_tracks;
}

void InstrumentsActions::newInstrumentTrack()
{
    tracks().addInstrumentTrack();
}

void InstrumentsActions::openInScore()
{
    QString error;
    if (!tracks().openLastTakeInScore(&error) && !error.isEmpty()) {
        interactive()->info("Score", error.toStdString());
    }
}

void InstrumentsActions::exportToScenes()
{
    QString error;
    if (!tracks().exportToScenes(&error) && !error.isEmpty()) {
        interactive()->info("Scenes", error.toStdString());
    }
}

void InstrumentsActions::openPanel()
{
    interactive()->open("audacity://instruments");
}

void InstrumentsActions::openSampler()
{
    interactive()->open("audacity://sampler");
}

// Studio: count-in clicks (sample-accurate, from the instruments' audio stream), then recording
// starts on the downbeat on the current track
void InstrumentsActions::recordWithCountIn(int bars)
{
    auto s = InstrumentsModule::service();
    if (!s) {
        return;
    }
    auto synth = s->synth();
    if (!synth->isRunning() && !synth->start()) {
        dispatcher()->dispatch("record-on-current-track"); // no audio for the click: just record
        return;
    }
    double bpm = 120.0;
    int beats = 4;
    if (auto te = globalContext()->currentTrackeditProject()) {
        const auto ts = te->timeSignature();
        if (ts.tempo > 0) {
            bpm = ts.tempo;
        }
        if (ts.upper > 0) {
            beats = ts.upper;
        }
    }
    synth->startCountIn(bpm, beats, bars);
    if (!m_countInPoll) {
        m_countInPoll = std::make_unique<QTimer>();
        m_countInPoll->setInterval(1);
        m_countInPoll->setTimerType(Qt::PreciseTimer);
        QObject::connect(m_countInPoll.get(), &QTimer::timeout, [this]() {
            auto s2 = InstrumentsModule::service();
            if (!s2 || !s2->synth()->isCountingIn()) {
                m_countInPoll->stop();
                dispatcher()->dispatch("record-on-current-track");
            }
        });
    }
    m_countInPoll->start();
}

void InstrumentsActions::panic()
{
    if (auto s = InstrumentsModule::service()) {
        s->synth()->allNotesOff();
    }
}

const UiActionList& InstrumentsActions::actionsList() const
{
    return s_actions;
}

bool InstrumentsActions::actionEnabled(const UiAction&) const
{
    return true;
}

async::Channel<ActionCodeList> InstrumentsActions::actionEnabledChanged() const
{
    return m_enabledChanged;
}

bool InstrumentsActions::actionChecked(const UiAction&) const
{
    return false;
}

async::Channel<ActionCodeList> InstrumentsActions::actionCheckedChanged() const
{
    return m_checkedChanged;
}
