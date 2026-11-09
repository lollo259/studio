/*
 * Tape (Laurence Who Studio): fold panels away and bring them back.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "panelsmodel.h"

#include "framework/actions/actiontypes.h"
#include "framework/dockwindow/idockwindow.h"

#include "appshell/appshelltypes.h"

using namespace au::appshell;
using namespace muse::actions;

PanelsModel::PanelsModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

QStringList PanelsModel::docks(const QString& group)
{
    if (group == "side") {
        return { TRACKS_PANEL_NAME };
    }
    if (group == "top") {
        return { PROJECT_TOOLBAR_NAME, UNDO_REDO_TOOLBAR_NAME, PLAYBACK_TOOLBAR_NAME, WORKSPACES_TOOLBAR_NAME };
    }
    if (group == "right") {
        return { HISTORY_PANEL_NAME, PLAYBACK_METER_PANEL_NAME };
    }
    return {};
}

void PanelsModel::init()
{
    if (m_listening) {
        return;
    }
    auto subscribe = [this]() {
        if (muse::dock::IDockWindow* w = dockWindowProvider()->window()) {
            w->docksOpenStatusChanged().onReceive(this, [this](const QStringList&) {
                emit changed();
            });
        }
        emit changed();
    };
    dockWindowProvider()->windowChanged().onNotify(this, subscribe);
    subscribe();
    m_listening = true;
}

bool PanelsModel::anyOpen(const QString& group) const
{
    const muse::dock::IDockWindow* w = dockWindowProvider()->window();
    if (!w) {
        return false;
    }
    for (const QString& d : docks(group)) {
        if (w->isDockOpen(d)) {
            return true;
        }
    }
    return false;
}

bool PanelsModel::sideOpen() const { return anyOpen("side"); }
bool PanelsModel::topOpen() const { return anyOpen("top"); }
bool PanelsModel::rightOpen() const { return anyOpen("right"); }

void PanelsModel::setOpen(const QString& dock, bool open)
{
    dispatcher()->dispatch("dock-set-open", ActionData::make_arg2<QString, bool>(dock, open));
}

void PanelsModel::toggle(const QString& group)
{
    const muse::dock::IDockWindow* w = dockWindowProvider()->window();
    if (!w) {
        return;
    }
    QStringList& hidden = group == "side" ? m_hiddenSide : group == "top" ? m_hiddenTop : m_hiddenRight;

    if (anyOpen(group)) {
        hidden.clear();
        for (const QString& d : docks(group)) {
            if (w->isDockOpen(d)) {
                hidden << d;
                setOpen(d, false);
            }
        }
    } else {
        QStringList back = hidden;
        if (back.isEmpty()) {
            back = group == "side" ? QStringList { TRACKS_PANEL_NAME }
                   : group == "top" ? QStringList { PROJECT_TOOLBAR_NAME, UNDO_REDO_TOOLBAR_NAME, PLAYBACK_TOOLBAR_NAME }
                   : QStringList { HISTORY_PANEL_NAME };
        }
        for (const QString& d : back) {
            setOpen(d, true);
        }
        hidden.clear();
    }
    emit changed();
}
