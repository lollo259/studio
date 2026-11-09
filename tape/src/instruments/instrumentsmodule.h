/*
 * Tape (Laurence Who Studio) — instruments
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <memory>

#include "modularity/imodulesetup.h"

namespace au::instruments {
class LiveSynth;
class InstrumentsService;
class InstrumentsActions;

class InstrumentsModule : public muse::modularity::IModuleSetup
{
public:
    std::string moduleName() const override;
    void registerExports() override;
    void resolveImports() override;
    void onInit(const muse::IApplication::RunMode& mode) override;
    void onDeinit() override;

    muse::modularity::IContextSetup* newContext(const muse::modularity::ContextPtr& ctx) const override;

    static std::shared_ptr<InstrumentsService> service();
};

class InstrumentsContext : public muse::modularity::IContextSetup
{
public:
    InstrumentsContext(const muse::modularity::ContextPtr& ctx)
        : muse::modularity::IContextSetup(ctx) {}

    void registerExports() override {}
    void onInit(const muse::IApplication::RunMode& mode) override;

private:
    std::shared_ptr<InstrumentsActions> m_actions;
    std::shared_ptr<class StudioLink> m_link;
};
}
