/*
 * Tape (Laurence Who Studio) — instruments
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "instrumentsmodule.h"

#include "framework/global/modularity/ioc.h"
#include "framework/interactive/iinteractiveuriregister.h"
#include "framework/ui/iuiactionsregister.h"

#include "internal/livesynth.h"
#include "internal/instrumentsservice.h"
#include "internal/instrumentsactions.h"
#include "internal/studiolink.h"
#include "internal/videoactions.h"

using namespace au::instruments;
using namespace muse;
using namespace muse::modularity;

static const std::string mname("instruments");

static std::shared_ptr<LiveSynth> s_synth;
static std::shared_ptr<InstrumentsService> s_service;

std::string InstrumentsModule::moduleName() const
{
    return mname;
}

std::shared_ptr<InstrumentsService> InstrumentsModule::service()
{
    return s_service;
}

void InstrumentsModule::registerExports()
{
    s_synth = std::make_shared<LiveSynth>();
    s_service = std::make_shared<InstrumentsService>(s_synth);
    globalIoc()->registerExport<ILiveSynth>(mname, s_synth);
}

void InstrumentsModule::resolveImports()
{
    auto ir = globalIoc()->resolve<muse::interactive::IInteractiveUriRegister>(mname);
    if (ir) {
        ir->registerQmlUri(muse::Uri("audacity://instruments"), "Audacity.Instruments", "InstrumentsDialog");
        ir->registerQmlUri(muse::Uri("audacity://sampler"), "Audacity.Instruments", "SamplerDialog");
        ir->registerQmlUri(muse::Uri("audacity://video"), "Audacity.Instruments", "VideoWindow");
    }
}

void InstrumentsModule::onInit(const IApplication::RunMode& mode)
{
    if (mode != IApplication::RunMode::GuiApp) {
        return;
    }
    s_service->init();
}

void InstrumentsModule::onDeinit()
{
    if (s_service) {
        s_service->deinit();
    }
}

IContextSetup* InstrumentsModule::newContext(const ContextPtr& ctx) const
{
    return new InstrumentsContext(ctx);
}

void InstrumentsContext::onInit(const IApplication::RunMode& mode)
{
    if (mode != IApplication::RunMode::GuiApp) {
        return;
    }
    m_actions = std::make_shared<InstrumentsActions>(iocContext());
    m_actions->init();

    // Video: a film locked to the playhead, for scoring to picture
    m_video = std::make_shared<VideoActions>(iocContext());
    m_video->init();

    // Studio: play, stop and the cursor shared live with Score
    m_link = std::make_shared<StudioLink>(iocContext());
    m_link->init();
    auto ar = ioc()->resolve<muse::ui::IUiActionsRegister>(mname);
    if (ar) {
        ar->reg(m_actions);
        ar->reg(m_video);
    }
}
