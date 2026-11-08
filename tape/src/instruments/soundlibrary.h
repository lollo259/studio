/*
 * Tape (Laurence Who Studio) — instruments
 * SoundLibrary: finds every playable sound in the Studio Sounds folder
 * (SFZ instruments and SF2/SF3 soundfonts) and sorts them into families
 * for the sound picker.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <string>
#include <vector>

namespace au::instruments {
enum class SoundFamily {
    Piano, Keys, Strings, Winds, Brass, Percussion, Voice, Synth, General, Other
};

struct SoundEntry {
    std::string name;      // shown in the picker, e.g. "Violin Section Sustain"
    std::string library;   // top folder in Studio\Sounds, e.g. "VSCO 2 CE"
    std::string path;      // absolute path to the .sfz / .sf2 / .sf3 file
    SoundFamily family = SoundFamily::Other;
    bool isSoundFont = false; // .sf2/.sf3 (many programs inside) vs single .sfz instrument
};

const char* familyName(SoundFamily f);

//! Default Studio sounds folder: %USERPROFILE%\Studio\Sounds (Windows), ~/Studio/Sounds elsewhere
std::string defaultSoundsFolder();

//! Scans the folder recursively. Entries are sorted by family, then library, then name.
std::vector<SoundEntry> scanSounds(const std::string& folder);
}
