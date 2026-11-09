/*
 * Tape (Laurence Who Studio) — instruments
 * The two sound engines behind one face: sfizz for SFZ instruments,
 * FluidSynth for SF2/SF3 soundfonts (one program at a time).
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <string>

namespace au::instruments {
class Engine
{
public:
    virtual ~Engine() = default;

    virtual void setSampleRate(float rate) = 0;
    virtual void noteOn(int note, int velocity) = 0;
    virtual void noteOff(int note) = 0;
    virtual void cc(int cc, int value) = 0;
    virtual void pitchWheel(int value) = 0; // -8192 .. 8191
    virtual void allSoundOff() = 0;
    //! renders n frames (n <= maxBlock) into two separate channels
    virtual void render(float* left, float* right, int n) = 0;
};

//! SFZ instrument from a file, or from text (built-in sounds). nullptr if it fails to load.
Engine* makeSfzEngine(float sampleRate, int maxBlock, const std::string& file);
Engine* makeSfzEngineFromText(float sampleRate, int maxBlock, const std::string& virtualPath, const std::string& text);

//! One program (bank/preset) of an SF2/SF3 soundfont. nullptr if it fails to load.
Engine* makeSoundFontEngine(float sampleRate, const std::string& file, int bank, int program);
}
