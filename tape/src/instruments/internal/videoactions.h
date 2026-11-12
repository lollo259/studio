/*
 * Tape (Laurence Who Studio) — video
 * "Import video…" and "Video window": a picture locked to Tape's playhead, for scoring to film.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include "framework/actions/actionable.h"
#include "framework/actions/iactionsdispatcher.h"
#include "framework/global/async/asyncable.h"
#include "framework/global/modularity/ioc.h"
#include "framework/interactive/iinteractive.h"
#include "framework/ui/iuiactionsmodule.h"
#include "context/iglobalcontext.h"

namespace au::instruments {
class VideoActions : public muse::ui::IUiActionsModule, public muse::actions::Actionable, public muse::async::Asyncable,
    public muse::Contextable
{
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher{ this };
    muse::ContextInject<muse::IInteractive> interactive{ this };
    muse::ContextInject<au::context::IGlobalContext> globalContext{ this };

public:
    VideoActions(const muse::modularity::ContextPtr& ctx)
        : muse::Contextable(ctx) {}

    void init();

    const muse::ui::UiActionList& actionsList() const override;
    bool actionEnabled(const muse::ui::UiAction& act) const override;
    muse::async::Channel<muse::actions::ActionCodeList> actionEnabledChanged() const override;
    bool actionChecked(const muse::ui::UiAction& act) const override;
    muse::async::Channel<muse::actions::ActionCodeList> actionCheckedChanged() const override;

private:
    void importVideo();
    void toggleWindow();
    void openWindow();
    bool windowOpened() const;
    void onProjectChanged();
    QString projectPath() const;

    muse::async::Channel<muse::actions::ActionCodeList> m_enabledChanged;
    muse::async::Channel<muse::actions::ActionCodeList> m_checkedChanged;
};
}
