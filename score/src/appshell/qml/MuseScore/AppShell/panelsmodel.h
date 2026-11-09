/*
 * Score (Laurence Who Studio): fold panels away and bring them back.
 *   side    palettes, layout/instruments, properties, selection filter, undo history
 *   top     the toolbars under the menu bar
 *   bottom  mixer, piano keyboard, timeline, percussion
 * The menu bar itself always stays.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <QObject>
#include <QStringList>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "rcommand/icommanddispatcher.h"
#include "dockwindow/idockwindowprovider.h"

namespace mu::appshell {
class PanelsModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool sideOpen READ sideOpen NOTIFY changed)
    Q_PROPERTY(bool topOpen READ topOpen NOTIFY changed)
    //! inside the Studio window: Studio has the window keys, the app hides its own
    Q_PROPERTY(bool embedded READ embedded CONSTANT)
    Q_PROPERTY(bool bottomOpen READ bottomOpen NOTIFY changed)

    muse::ContextInject<muse::rcommand::ICommandDispatcher> commandDispatcher = { this };
    muse::ContextInject<muse::dock::IDockWindowProvider> dockWindowProvider = { this };

public:
    explicit PanelsModel(QObject* parent = nullptr);

    Q_INVOKABLE void init();
    Q_INVOKABLE void toggle(const QString& group);

    bool embedded() const { return qEnvironmentVariableIsSet("STUDIO_EMBEDDED"); }

    bool sideOpen() const;
    bool topOpen() const;
    bool bottomOpen() const;

signals:
    void changed();

private:
    static QStringList docks(const QString& group);
    bool anyOpen(const QString& group) const;
    void setOpen(const QString& dock, bool open);

    QStringList m_hiddenSide;
    QStringList m_hiddenTop;
    QStringList m_hiddenBottom;
    bool m_listening = false;
};
}
