/*
 * Score (Laurence Who Studio): fold panels away and bring them back.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "panelsmodel.h"

#include "dockwindow/idockwindow.h"
#include "dockwindow_v2/dockcommands.h"

#include "appshell/appshelltypes.h"

using namespace mu::appshell;

PanelsModel::PanelsModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

QStringList PanelsModel::docks(const QString& group)
{
    if (group == "side") {
        return { PALETTES_PANEL_NAME, LAYOUT_PANEL_NAME, PROPERTIES_PANEL_NAME, SELECTION_FILTERS_PANEL_NAME, UNDO_HISTORY_PANEL_NAME };
    }
    if (group == "top") {
        return { NOTATION_TOOLBAR_NAME, UNDO_REDO_TOOLBAR_NAME, NOTE_INPUT_BAR_NAME, PLAYBACK_TOOLBAR_NAME, EXTENSIONS_TOOLBAR_NAME };
    }
    if (group == "bottom") {
        return { MIXER_PANEL_NAME, PIANO_KEYBOARD_PANEL_NAME, TIMELINE_PANEL_NAME, PERCUSSION_PANEL_NAME };
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
bool PanelsModel::bottomOpen() const { return anyOpen("bottom"); }

void PanelsModel::setOpen(const QString& dock, bool open)
{
    commandDispatcher()->dispatch(muse::dock::DOCK_SET_OPEN_COMMAND,
                                  { { "dock_name", muse::Val(dock) }, { "open", muse::Val(open) } });
}

void PanelsModel::toggle(const QString& group)
{
    const muse::dock::IDockWindow* w = dockWindowProvider()->window();
    if (!w) {
        return;
    }
    QStringList& hidden = group == "side" ? m_hiddenSide : group == "top" ? m_hiddenTop : m_hiddenBottom;

    if (anyOpen(group)) {
        // fold: remember what was open, then close it
        hidden.clear();
        for (const QString& d : docks(group)) {
            if (w->isDockOpen(d)) {
                hidden << d;
                setOpen(d, false);
            }
        }
    } else {
        // unfold: bring back what was there (or the usual one)
        QStringList back = hidden;
        if (back.isEmpty()) {
            if (group == "side") {
                back = { PALETTES_PANEL_NAME };
            } else if (group == "top") {
                back = { NOTATION_TOOLBAR_NAME, UNDO_REDO_TOOLBAR_NAME, NOTE_INPUT_BAR_NAME, PLAYBACK_TOOLBAR_NAME };
            } else {
                back = { MIXER_PANEL_NAME };
            }
        }
        for (const QString& d : back) {
            setOpen(d, true);
        }
        hidden.clear();
    }
    emit changed();
}
