/*
 * Tape (Laurence Who Studio) — instruments
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "instrumentspanelmodel.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantMap>

#include "framework/actions/actiontypes.h"

#include "instrumentsmodule.h"
#include "internal/instrumentsservice.h"

using namespace au::instruments;

namespace {
std::shared_ptr<InstrumentsService> svc()
{
    return InstrumentsModule::service();
}

QString noteName(int n)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (n < 0) {
        return QString();
    }
    return QString("%1%2").arg(names[n % 12]).arg(n / 12 - 1);
}
}

InstrumentsPanelModel::InstrumentsPanelModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    m_timer.setInterval(200);
    connect(&m_timer, &QTimer::timeout, this, &InstrumentsPanelModel::tick);
}

void InstrumentsPanelModel::load()
{
    if (auto s = svc()) {
        s->changed().onNotify(this, [this]() { emit changed(); });
        if (!s->synth()->isRunning()) {
            s->synth()->start();
        }
    }
    m_timer.start();
    emit changed();
}

QVariantList InstrumentsPanelModel::sounds() const
{
    QVariantList list;
    auto s = svc();
    if (!s) {
        return list;
    }
    const auto& all = s->sounds();
    for (size_t i = 0; i < all.size(); ++i) {
        QVariantMap m;
        m["index"] = int(i);
        m["name"] = QString::fromStdString(all[i].name);
        m["library"] = QString::fromStdString(all[i].library);
        m["family"] = QString::fromUtf8(familyName(all[i].family));
        m["soundfont"] = all[i].isSoundFont;
        list << m;
    }
    return list;
}

QString InstrumentsPanelModel::currentSound() const
{
    auto s = svc();
    return s ? QString::fromStdString(s->synth()->currentSoundName()) : QString();
}

bool InstrumentsPanelModel::loading() const
{
    auto s = svc();
    return s && s->synth()->isLoading();
}

QString InstrumentsPanelModel::status() const
{
    auto s = svc();
    return s ? QString::fromStdString(s->synth()->statusText()) : QString("instruments unavailable");
}

QVariantList InstrumentsPanelModel::keyboards() const
{
    QVariantList list;
    if (auto s = svc()) {
        for (const auto& k : s->keyboards()) {
            QVariantMap m;
            m["value"] = QString::fromStdString(k.first);
            m["text"] = QString::fromStdString(k.second);
            list << m;
        }
    }
    return list;
}

QString InstrumentsPanelModel::currentKeyboard() const
{
    auto s = svc();
    return s ? QString::fromStdString(s->currentKeyboard()) : QString();
}

double InstrumentsPanelModel::volume() const
{
    auto s = svc();
    return s ? s->synth()->volume() : 0.0;
}

void InstrumentsPanelModel::setVolume(double v)
{
    if (auto s = svc()) {
        s->synth()->setVolume(float(v));
        emit changed();
    }
}

bool InstrumentsPanelModel::taking() const
{
    auto s = svc();
    return s && s->synth()->isTaking();
}

QString InstrumentsPanelModel::takeTime() const
{
    auto s = svc();
    const int secs = s ? int(s->synth()->takeSeconds()) : 0;
    return QString("%1:%2").arg(secs / 60).arg(secs % 60, 2, 10, QChar('0'));
}

QString InstrumentsPanelModel::lastNote() const
{
    auto s = svc();
    return s ? noteName(s->lastNote()) : QString();
}

QString InstrumentsPanelModel::soundsFolder() const
{
    auto s = svc();
    return s ? QString::fromStdString(s->soundsFolder()) : QString();
}

QString InstrumentsPanelModel::lastTake() const
{
    return m_lastTake;
}

QString InstrumentsPanelModel::takesFolder() const
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Studio/Takes";
}

void InstrumentsPanelModel::selectSound(int index)
{
    if (auto s = svc()) {
        s->selectSound(size_t(index));
    }
}

void InstrumentsPanelModel::selectKeyboard(const QString& id)
{
    if (auto s = svc()) {
        s->selectKeyboard(id.toStdString());
    }
}

InstrumentTracks& InstrumentsPanelModel::tracks()
{
    if (!m_tracks) {
        m_tracks = std::make_unique<InstrumentTracks>(iocContext());
    }
    return *m_tracks;
}

void InstrumentsPanelModel::toggleTake()
{
    if (!tracks().isTaking()) {
        // on an instrument track: plays the project along and lands in that track, in time
        const QString name = "take " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH.mm.ss");
        tracks().startTake(takesFolder(), name);
    } else {
        m_lastTake = tracks().stopTake();
    }
    emit changed();
}

void InstrumentsPanelModel::newInstrumentTrack()
{
    tracks().addInstrumentTrack();
}

void InstrumentsPanelModel::openLastTakeInScore()
{
    tracks().openLastTakeInScore();
}

void InstrumentsPanelModel::rescan()
{
    if (auto s = svc()) {
        s->rescan();
    }
}

void InstrumentsPanelModel::openSoundsFolder()
{
    QDir().mkpath(soundsFolder());
    QDesktopServices::openUrl(QUrl::fromLocalFile(soundsFolder()));
}

void InstrumentsPanelModel::openTakesFolder()
{
    QDir().mkpath(takesFolder());
    QDesktopServices::openUrl(QUrl::fromLocalFile(takesFolder()));
}

void InstrumentsPanelModel::restartAudio()
{
    if (auto s = svc()) {
        s->synth()->stop();
        s->synth()->start();
    }
    emit changed();
}

void InstrumentsPanelModel::allNotesOff()
{
    if (auto s = svc()) {
        s->synth()->allNotesOff();
    }
}

void InstrumentsPanelModel::previewNote(int note, bool on)
{
    if (auto s = svc()) {
        if (on) {
            s->synth()->noteOn(note, 90);
        } else {
            s->synth()->noteOff(note);
        }
    }
}
