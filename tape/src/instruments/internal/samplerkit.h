/*
 * Tape (Laurence Who Studio) — instruments
 * Sampler kit: 16 pads, each holding one sound with a start/end trim and a play mode.
 * A kit is played by the live synth as a generated SFZ file (one region per pad)
 * and its state is kept next to it as JSON so the Sampler panel can reload it.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <array>
#include <vector>

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace au::instruments {
enum class PadMode {
    OneShot, // plays to the end, whatever you do
    Gate,    // plays while the pad/key is held
    Loop,    // loops between start and end while held
    Keys     // pitched: also playable chromatically from the keyboard
};

constexpr int SAMPLER_PADS = 16;
constexpr int SAMPLER_FIRST_KEY = 36;   // C2: pad 1; pad 16 is D#3 (51)
constexpr int SAMPLER_KEYS_LO = 52;     // E3: the keys-mode pad is spread from here...
constexpr int SAMPLER_KEYS_HI = 108;    // ...up to C8
constexpr int SAMPLER_KEYS_CENTER = 60; // C4 plays it at its own pitch

struct SamplerPad {
    QString name;     // shown on the pad
    QString file;     // absolute path of the sound
    double start = 0.0; // trim, 0..1 of the file length
    double end = 1.0;
    PadMode mode = PadMode::OneShot;
    bool choke = false; // pads in the choke group cut each other off
    qint64 frames = 0;  // file length in frames when known (WAV), 0 = unknown (no trim)

    bool isEmpty() const { return file.isEmpty(); }
};

struct SamplerKit {
    QString name;
    std::array<SamplerPad, SAMPLER_PADS> pads;
};

struct WavPeaks {
    bool ok = false;
    qint64 frames = 0;
    int sampleRate = 0;
    int channels = 0;
    std::vector<float> peaks; // min,max pairs (-1..1), mono mix
};

//! Reads a WAV file (16/24/32-bit PCM, 32-bit float; any channel count) into min/max peak pairs.
WavPeaks readWavPeaks(const QString& path, int pairs = 512);

const char* padModeId(PadMode m); // "one-shot", "gate", "loop", "keys"
PadMode padModeFromId(const QString& id);

//! Documents/Studio/Sampler
QString samplerFolder();
QString safeKitName(const QString& name);
QString kitSfzPath(const QString& kitName);
QString kitJsonPath(const QString& kitName);
QStringList kitNames(); // kits saved in the Sampler folder, sorted

QString makeSfz(const SamplerKit& kit);
QByteArray kitToJson(const SamplerKit& kit);
bool kitFromJson(const QByteArray& json, SamplerKit& kit);

//! Writes <kit>.sfz and <kit>.studiokit.json in the Sampler folder.
bool saveKit(const SamplerKit& kit, QString* error = nullptr);
bool loadKit(const QString& kitName, SamplerKit& kit);
void removeKitFiles(const QString& kitName);
}
