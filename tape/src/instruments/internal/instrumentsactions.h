/*
 * Tape (Laurence Who Studio) — instruments
 * The "Instruments" menu action: opens the instrument panel.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include "framework/actions/actionable.h"
#include "framework/actions/iactionsdispatcher.h"
#include "framework/global/async/asyncable.h"
#include "framework/global/modularity/ioc.h"
#include "framework/interactive/iinteractive.h"
#include "framework/ui/iuiactionsmodule.h"

#include <memory>

#include <QTimer>

#include "context/iglobalcontext.h"
#include "trackedit/iselectioncontroller.h"
#include "trackedit/itrackeditinteraction.h"

#include "instrumenttracks.h"

namespace au::instruments {
class InstrumentsActions : public muse::ui::IUiActionsModule, public muse::actions::Actionable, public muse::async::Asyncable,
    public muse::Contextable
{
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher{ this };
    muse::ContextInject<muse::IInteractive> interactive{ this };
    muse::ContextInject<au::context::IGlobalContext> globalContext{ this };
    muse::ContextInject<au::trackedit::ISelectionController> selectionController{ this };
    muse::ContextInject<au::trackedit::ITrackeditInteraction> trackeditInteraction{ this };

public:
    InstrumentsActions(const muse::modularity::ContextPtr& ctx)
        : muse::Contextable(ctx) {}

    void init();

    const muse::ui::UiActionList& actionsList() const override;
    bool actionEnabled(const muse::ui::UiAction& act) const override;
    muse::async::Channel<muse::actions::ActionCodeList> actionEnabledChanged() const override;
    bool actionChecked(const muse::ui::UiAction& act) const override;
    muse::async::Channel<muse::actions::ActionCodeList> actionCheckedChanged() const override;

private:
    void openPanel();
    void openSampler();
    void panic();
    void newInstrumentTrack();
    void openInScore();
    void recordWithCountIn(int bars);
    void playInScore();
    void takeToScore();
    std::unique_ptr<QTimer> m_countInPoll;
    InstrumentTracks& tracks();

    std::unique_ptr<InstrumentTracks> m_tracks;

    muse::async::Channel<muse::actions::ActionCodeList> m_enabledChanged;
    muse::async::Channel<muse::actions::ActionCodeList> m_checkedChanged;
};
}
