/*
 * Tape (Laurence Who Studio) — instruments
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "instrumentsservice.h"

#include <QSettings>
#include <QString>

#include "framework/midi/midievent.h"

using namespace au::instruments;
using namespace muse::midi;

namespace {
const char* KEY_SOUND = "instruments/sound";
const char* KEY_KEYBOARD = "instruments/keyboard";
const char* KEY_VOLUME = "instruments/volume";
}

InstrumentsService::InstrumentsService(std::shared_ptr<ILiveSynth> synth)
    : m_synth(std::move(synth))
{
}

void InstrumentsService::init()
{
    QSettings s("LaurenceWho", "Tape");
    m_synth->setVolume(s.value(KEY_VOLUME, 0.9).toFloat());

    rescan();
    const std::string lastSound = s.value(KEY_SOUND).toString().toStdString();
    for (size_t i = 0; i < m_sounds.size(); ++i) {
        if (m_sounds[i].path == lastSound) {
            m_synth->loadSound(m_sounds[i]);
            break;
        }
    }

    m_synth->start();

    if (midiInPort()) {
        midiInPort()->eventReceived().onReceive(this, [this](tick_t, const Event& e) {
            onMidi(e);
        });
        midiInPort()->availableDevicesChanged().onNotify(this, [this]() {
            if (!midiInPort()->isConnected()) {
                autoConnect();
            }
            m_changed.notify();
        });
        autoConnect();
    }

    m_synth->stateChanged().onNotify(this, [this]() {
        m_changed.notify();
    });
}

void InstrumentsService::deinit()
{
    QSettings s("LaurenceWho", "Tape");
    s.setValue(KEY_VOLUME, m_synth->volume());
    m_synth->allNotesOff();
    m_synth->stop();
}

std::string InstrumentsService::soundsFolder() const
{
    return defaultSoundsFolder();
}

void InstrumentsService::rescan()
{
    m_sounds = m_synth->builtinSounds();
    std::vector<SoundEntry> found = scanSounds(defaultSoundsFolder());
    m_sounds.insert(m_sounds.end(), found.begin(), found.end());
    m_changed.notify();
}

void InstrumentsService::selectSound(size_t index)
{
    if (index >= m_sounds.size()) {
        return;
    }
    m_synth->allNotesOff();
    m_synth->loadSound(m_sounds[index]);
    QSettings s("LaurenceWho", "Tape");
    s.setValue(KEY_SOUND, QString::fromStdString(m_sounds[index].path));
}

std::vector<std::pair<std::string, std::string> > InstrumentsService::keyboards() const
{
    std::vector<std::pair<std::string, std::string> > list;
    if (!midiInPort()) {
        return list;
    }
    for (const MidiDevice& d : midiInPort()->availableDevices()) {
        if (d.id == NONE_DEVICE_ID) {
            continue;
        }
        list.push_back({ d.id, d.name });
    }
    return list;
}

std::string InstrumentsService::currentKeyboard() const
{
    if (!midiInPort() || !midiInPort()->isConnected()) {
        return std::string();
    }
    return midiInPort()->deviceID();
}

void InstrumentsService::selectKeyboard(const std::string& id)
{
    if (!midiInPort()) {
        return;
    }
    midiInPort()->connect(id);
    QSettings s("LaurenceWho", "Tape");
    s.setValue(KEY_KEYBOARD, QString::fromStdString(id));
    m_changed.notify();
}

void InstrumentsService::autoConnect()
{
    const auto list = keyboards();
    if (list.empty()) {
        return;
    }
    QSettings s("LaurenceWho", "Tape");
    const std::string saved = s.value(KEY_KEYBOARD).toString().toStdString();
    for (const auto& k : list) {
        if (k.first == saved) {
            midiInPort()->connect(k.first);
            return;
        }
    }
    midiInPort()->connect(list.front().first);
}

void InstrumentsService::onMidi(const Event& e)
{
    for (const Event& ev : e.toMIDI10()) {
        unsigned char b[3] = { 0, 0, 0 };
        if (ev.toMidi10Bytes(b) == 0) {
            continue;
        }
        const int status = b[0] & 0xF0;
        switch (status) {
        case 0x90:
            if (b[2] > 0) {
                m_synth->noteOn(b[1], b[2]);
                m_lastNote = b[1];
            } else {
                m_synth->noteOff(b[1]);
            }
            break;
        case 0x80:
            m_synth->noteOff(b[1]);
            break;
        case 0xB0:
            if (b[1] == 123 || b[1] == 120) {
                m_synth->allNotesOff();
            } else {
                m_synth->controlChange(b[1], b[2]);
            }
            break;
        case 0xE0:
            m_synth->pitchBend(((b[2] << 7) | b[1]) - 8192);
            break;
        default:
            break;
        }
    }
}
