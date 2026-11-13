/*
 * Tape (Laurence Who Studio) — instruments
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "instrumentsactions.h"

#include <QFileInfo>

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
static const ActionCode COUNT_IN_1_CODE("record-count-in-1");
static const ActionCode PLAY_IN_SCORE_CODE("play-in-score");
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
    UiAction(PLAY_IN_SCORE_CODE,
             au::context::UiCtxAny,
             au::context::CTX_ANY,
             TranslatableString("action", "Play in Score as…"),
             TranslatableString("action_description", "This track (your recording) plays an instrument of the score in place of Score's sound")),
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
    dispatcher()->reg(this, COUNT_IN_1_CODE, [this]() { recordWithCountIn(1); });
    dispatcher()->reg(this, PLAY_IN_SCORE_CODE, this, &InstrumentsActions::playInScore);
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

void InstrumentsActions::openPanel()
{
    interactive()->open("audacity://instruments");
}

void InstrumentsActions::openSampler()
{
    interactive()->open("audacity://sampler");
}

// Studio: the selected track (a recording) plays one of the score's instruments: it is renamed
// "<instrument> · recording"; the Studio link then mutes that instrument in Score and Score's own
// rendering of it in Tape. Choosing it again on such a track gives the instrument back to Score.
void InstrumentsActions::playInScore()
{
    auto te = globalContext()->currentTrackeditProject();
    auto project = globalContext()->currentProject();
    if (!te || !project) {
        return;
    }
    const auto sel = selectionController()->selectedTracks();
    if (sel.size() != 1) {
        interactive()->info("Play in Score", muse::IInteractive::Text("Select the track with your recording first."));
        return;
    }
    const auto id = sel.front();
    QString current;
    for (const auto& t : te->trackList()) {
        if (t.id == id) {
            current = t.title.toQString();
        }
    }
    const QString suffix = QStringLiteral(" · recording");
    if (current.endsWith(suffix)) {
        // give the instrument back to Score
        const QString part = current.left(current.size() - suffix.size()).trimmed();
        trackeditInteraction()->changeTrackTitle(id, muse::String::fromQString(part + " take"));
        return;
    }
    // the score's instruments = Score's tracks in this project: "<score title>-<instrument>"
    const QString base = QFileInfo(project->path().toQString()).completeBaseName() + "-";
    QStringList parts;
    for (const auto& t : te->trackList()) {
        const QString title = t.title.toQString();
        if (title.startsWith(base) && t.id != id) {
            parts << title.mid(base.size());
        }
    }
    if (parts.isEmpty()) {
        interactive()->info("Play in Score", muse::IInteractive::Text("This project has no instruments from a score."));
        return;
    }
    muse::IInteractive::ButtonDatas buttons;
    for (int i = 0; i < parts.size() && i < 12; ++i) {
        buttons.push_back(muse::IInteractive::ButtonData(int(muse::IInteractive::Button::CustomButton) + 1 + i,
                                                         parts.at(i).toStdString()));
    }
    buttons.push_back(interactive()->buttonData(muse::IInteractive::Button::Cancel));
    interactive()->question("Play in Score", muse::IInteractive::Text("Which instrument does this recording play?"), buttons)
    .onResolve(this, [this, id, parts](const muse::IInteractive::Result& res) {
        const int i = res.button() - int(muse::IInteractive::Button::CustomButton) - 1;
        if (i >= 0 && i < parts.size()) {
            trackeditInteraction()->changeTrackTitle(id, muse::String::fromQString(parts.at(i) + " · recording"));
        }
    });
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
