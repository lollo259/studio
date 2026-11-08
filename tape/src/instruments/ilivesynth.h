/*
 * Tape (Laurence Who Studio) — instruments
 * ILiveSynth: the virtual instrument you play from a MIDI keyboard.
 * One sound at a time for now (layers and splits come later).
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <string>
#include <vector>

#include "framework/global/modularity/imoduleinterface.h"
#include "framework/global/async/notification.h"

#include "soundlibrary.h"

namespace au::instruments {
struct TakeResult {
    std::string audioPath; // 32-bit float WAV of what you played
    std::string midiPath;  // the same performance as a MIDI file (for the Score link)
    double seconds = 0.0;
};

class ILiveSynth : MODULE_GLOBAL_INTERFACE
{
    INTERFACE_ID(au::instruments::ILiveSynth)

public:
    virtual ~ILiveSynth() = default;

    //! Opens the audio output (lowest latency the system offers). Safe to call again.
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    virtual std::string statusText() const = 0;   // human readable, for the panel
    virtual double latencyMs() const = 0;

    //! Loads a sound in the background; the old one keeps playing until the new one is ready.
    virtual void loadSound(const SoundEntry& sound) = 0;
    //! Built-in sounds that always work, even with an empty Sounds folder.
    virtual std::vector<SoundEntry> builtinSounds() const = 0;
    virtual std::string currentSoundName() const = 0;
    virtual bool isLoading() const = 0;

    //! Fires (on the main thread) when a sound finished loading, the device changed, etc.
    virtual muse::async::Notification stateChanged() const = 0;

    // Live input (main thread)
    virtual void noteOn(int note, int velocity) = 0;
    virtual void noteOff(int note) = 0;
    virtual void controlChange(int cc, int value) = 0;
    virtual void pitchBend(int value) = 0; // -8192..8191
    virtual void allNotesOff() = 0;

    virtual void setVolume(float linear) = 0; // 0..1.5
    virtual float volume() const = 0;

    // Takes: everything you play is written to disk while a take runs
    virtual bool startTake(const std::string& folder, const std::string& baseName) = 0;
    virtual TakeResult stopTake() = 0;
    virtual bool isTaking() const = 0;
    virtual double takeSeconds() const = 0;
};
}
