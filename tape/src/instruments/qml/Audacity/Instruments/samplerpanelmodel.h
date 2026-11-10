/*
 * Tape (Laurence Who Studio) — instruments
 * Model behind the Sampler panel: 16 pads, each with one trimmed sound,
 * played through the live synth as a generated SFZ kit.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <array>

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "framework/global/async/asyncable.h"
#include "framework/global/modularity/ioc.h"
#include "framework/interactive/iinteractive.h"

#include "instruments/internal/samplerkit.h"

namespace au::instruments {
class SamplerPanelModel : public QObject, public muse::async::Asyncable, public muse::Contextable
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString kitName READ kitName WRITE setKitName NOTIFY kitChanged)
    Q_PROPERTY(QStringList kits READ kits NOTIFY kitChanged)
    Q_PROPERTY(QVariantList pads READ pads NOTIFY padsChanged)
    Q_PROPERTY(int selectedPad READ selectedPad WRITE selectPad NOTIFY selectionChanged)
    Q_PROPERTY(QString padName READ padName WRITE setPadName NOTIFY selectionChanged)
    Q_PROPERTY(QString padFile READ padFile NOTIFY selectionChanged)
    Q_PROPERTY(bool padLoaded READ padLoaded NOTIFY selectionChanged)
    Q_PROPERTY(bool padCanTrim READ padCanTrim NOTIFY selectionChanged)
    Q_PROPERTY(double padStart READ padStart WRITE setPadStart NOTIFY selectionChanged)
    Q_PROPERTY(double padEnd READ padEnd WRITE setPadEnd NOTIFY selectionChanged)
    Q_PROPERTY(QString padMode READ padMode WRITE setPadMode NOTIFY selectionChanged)
    Q_PROPERTY(bool padChoke READ padChoke WRITE setPadChoke NOTIFY selectionChanged)
    Q_PROPERTY(QString padInfo READ padInfo NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList peaks READ peaks NOTIFY selectionChanged)
    Q_PROPERTY(int litMask READ litMask NOTIFY litChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString folder READ folder CONSTANT)

    muse::ContextInject<muse::IInteractive> interactive{ this };

public:
    explicit SamplerPanelModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();
    Q_INVOKABLE void previewPad(int index, bool on);
    Q_INVOKABLE void chooseSound(int index);              // file dialog
    Q_INVOKABLE bool dropOnPad(int index, const QVariant& urls); // drag and drop
    Q_INVOKABLE void clearPad();
    Q_INVOKABLE void resetTrim();
    Q_INVOKABLE void newKit();
    Q_INVOKABLE void openKit(const QString& name);
    Q_INVOKABLE void openFolder();
    Q_INVOKABLE void allNotesOff();
    Q_INVOKABLE QString keyName(int index) const;

    QString kitName() const;
    void setKitName(const QString& name);
    QStringList kits() const;
    QVariantList pads() const;
    int selectedPad() const;
    void selectPad(int index);
    QString padName() const;
    void setPadName(const QString& name);
    QString padFile() const;
    bool padLoaded() const;
    bool padCanTrim() const;
    double padStart() const;
    void setPadStart(double v);
    double padEnd() const;
    void setPadEnd(double v);
    QString padMode() const;
    void setPadMode(const QString& mode);
    bool padChoke() const;
    void setPadChoke(bool on);
    QString padInfo() const;
    QVariantList peaks() const;
    int litMask() const;
    QString status() const;
    QString folder() const;

signals:
    void kitChanged();
    void padsChanged();
    void selectionChanged();
    void litChanged();
    void statusChanged();

private:
    SamplerPad& pad();
    const SamplerPad& pad() const;
    bool setPadFile(int index, const QString& path);
    void readPeaks(int index);
    void changed(bool padList = true);
    void apply(); // save the kit and load it into the live synth
    void pollLit();

    SamplerKit m_kit;
    std::array<WavPeaks, SAMPLER_PADS> m_peaks;
    int m_selected = 0;
    int m_lit = 0;
    int m_pressed = 0; // pads held with the mouse
    QString m_error;
    QTimer m_applyTimer;
    QTimer m_litTimer;
};
}
