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
static const ActionCode PANIC_CODE("instruments-panic");

static const UiActionList s_actions = {
    UiAction(OPEN_CODE,
             au::context::UiCtxAny,
             au::context::CTX_ANY,
             TranslatableString("action", "&Instruments…"),
             TranslatableString("action_description", "Play virtual instruments from your MIDI keyboard")),
    UiAction(PANIC_CODE,
             au::context::UiCtxAny,
             au::context::CTX_ANY,
             TranslatableString("action", "All notes off"),
             TranslatableString("action_description", "Stop every sounding note of the instruments")),
};

void InstrumentsActions::init()
{
    dispatcher()->reg(this, OPEN_CODE, this, &InstrumentsActions::openPanel);
    dispatcher()->reg(this, PANIC_CODE, this, &InstrumentsActions::panic);
}

void InstrumentsActions::openPanel()
{
    interactive()->open("audacity://instruments");
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
