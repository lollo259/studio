/*
 * Tape (Laurence Who Studio) — instruments
 * Model behind the Instruments panel.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "framework/actions/iactionsdispatcher.h"
#include "framework/global/async/asyncable.h"
#include "framework/global/modularity/ioc.h"

namespace au::instruments {
class InstrumentsPanelModel : public QObject, public muse::async::Asyncable, public muse::Contextable
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVariantList sounds READ sounds NOTIFY changed)
    Q_PROPERTY(QString currentSound READ currentSound NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QVariantList keyboards READ keyboards NOTIFY changed)
    Q_PROPERTY(QString currentKeyboard READ currentKeyboard NOTIFY changed)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY changed)
    Q_PROPERTY(bool taking READ taking NOTIFY changed)
    Q_PROPERTY(QString takeTime READ takeTime NOTIFY tick)
    Q_PROPERTY(QString lastNote READ lastNote NOTIFY tick)
    Q_PROPERTY(QString soundsFolder READ soundsFolder CONSTANT)
    Q_PROPERTY(QString lastTake READ lastTake NOTIFY changed)

    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher{ this };

public:
    explicit InstrumentsPanelModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();
    Q_INVOKABLE void selectSound(int index);
    Q_INVOKABLE void selectKeyboard(const QString& id);
    Q_INVOKABLE void toggleTake();
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void openSoundsFolder();
    Q_INVOKABLE void openTakesFolder();
    Q_INVOKABLE void restartAudio();
    Q_INVOKABLE void allNotesOff();
    Q_INVOKABLE void previewNote(int note, bool on);

    QVariantList sounds() const;
    QString currentSound() const;
    bool loading() const;
    QString status() const;
    QVariantList keyboards() const;
    QString currentKeyboard() const;
    double volume() const;
    void setVolume(double v);
    bool taking() const;
    QString takeTime() const;
    QString lastNote() const;
    QString soundsFolder() const;
    QString lastTake() const;

signals:
    void changed();
    void tick();

private:
    QString takesFolder() const;

    QTimer m_timer;
    QString m_lastTake;
};
}
