/*
 * Tape (Laurence Who Studio) — instruments
 * InstrumentsService: connects your MIDI keyboard to the live synth,
 * keeps the list of sounds, remembers your choices.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <array>
#include <string>
#include <vector>

#include "framework/global/async/asyncable.h"
#include "framework/global/async/notification.h"
#include "framework/global/modularity/ioc.h"
#include "framework/midi/imidiinport.h"

#include "../ilivesynth.h"
#include "../soundlibrary.h"

namespace au::instruments {
class InstrumentsService : public muse::async::Asyncable
{
    muse::GlobalInject<muse::midi::IMidiInPort> midiInPort;

public:
    explicit InstrumentsService(std::shared_ptr<ILiveSynth> synth);

    void init();
    void deinit();

    std::shared_ptr<ILiveSynth> synth() const { return m_synth; }

    // sounds
    void rescan();
    const std::vector<SoundEntry>& sounds() const { return m_sounds; }
    std::string soundsFolder() const;
    void selectSound(size_t index);

    // keyboard
    std::vector<std::pair<std::string, std::string> > keyboards() const; // id, name
    std::string currentKeyboard() const;
    void selectKeyboard(const std::string& id);

    int lastNote() const { return m_lastNote; }
    //! Whether a key of the MIDI keyboard is down right now (the Sampler lights its pads with it)
    bool isNoteHeld(int note) const { return note >= 0 && note < 128 && m_held[size_t(note)]; }
    muse::async::Notification changed() const { return m_changed; }

private:
    void autoConnect();
    void onMidi(const muse::midi::Event& e);

    std::shared_ptr<ILiveSynth> m_synth;
    std::vector<SoundEntry> m_sounds;
    int m_lastNote = -1;
    std::array<bool, 128> m_held {};
    bool m_sustain = false;
    muse::async::Notification m_changed;
};
}
