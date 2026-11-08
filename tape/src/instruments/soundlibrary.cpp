/*
 * Tape (Laurence Who Studio) — instruments
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "soundlibrary.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;
using namespace au::instruments;

const char* au::instruments::familyName(SoundFamily f)
{
    switch (f) {
    case SoundFamily::Piano: return "Piano";
    case SoundFamily::Keys: return "Keys";
    case SoundFamily::Strings: return "Strings";
    case SoundFamily::Winds: return "Winds";
    case SoundFamily::Brass: return "Brass";
    case SoundFamily::Percussion: return "Percussion";
    case SoundFamily::Voice: return "Voice";
    case SoundFamily::Synth: return "Synth";
    case SoundFamily::General: return "General MIDI";
    case SoundFamily::Other: break;
    }
    return "Other";
}

std::string au::instruments::defaultSoundsFolder()
{
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    return (fs::path(home ? home : ".") / "Studio" / "Sounds").string();
}

static std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

static bool has(const std::string& s, std::initializer_list<const char*> words)
{
    for (const char* w : words) {
        if (s.find(w) != std::string::npos) {
            return true;
        }
    }
    return false;
}

static SoundFamily guessFamily(const std::string& pathLower, bool soundFont)
{
    // order matters: "piano" before "keys", "organ" is keys, "harp" is strings
    if (has(pathLower, { "piano", "grand", "upright", "steinway", "yamaha c5" })) return SoundFamily::Piano;
    if (has(pathLower, { "organ", "harpsichord", "celesta", "rhodes", "wurli", "clav", "keys", "glock", "marimba", "xylo", "vibraphone" })) return SoundFamily::Keys;
    if (has(pathLower, { "violin", "viola", "cello", "contrabass", "double bass", "strings", "string", "harp", "pizz", "guitar" })) return SoundFamily::Strings;
    if (has(pathLower, { "flute", "piccolo", "oboe", "clarinet", "bassoon", "recorder", "sax", "winds", "woodwind" })) return SoundFamily::Winds;
    if (has(pathLower, { "horn", "trumpet", "trombone", "tuba", "brass", "cornet" })) return SoundFamily::Brass;
    if (has(pathLower, { "drum", "perc", "timpani", "snare", "cymbal", "tam", "bell", "kit" })) return SoundFamily::Percussion;
    if (has(pathLower, { "choir", "voice", "vocal", "soprano", "alto", "tenor" })) return SoundFamily::Voice;
    if (has(pathLower, { "synth", "pad", "lead", "saw" })) return SoundFamily::Synth;
    if (soundFont) return SoundFamily::General;
    return SoundFamily::Other;
}

static std::string prettyName(const fs::path& file)
{
    std::string n = file.stem().string();
    std::replace(n.begin(), n.end(), '_', ' ');
    std::replace(n.begin(), n.end(), '-', ' ');
    return n;
}

std::vector<SoundEntry> au::instruments::scanSounds(const std::string& folder)
{
    std::vector<SoundEntry> out;
    std::error_code ec;
    const fs::path root(folder);
    if (!fs::is_directory(root, ec)) {
        return out;
    }
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied | fs::directory_options::follow_directory_symlink, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) {
            continue;
        }
        const std::string ext = lower(it->path().extension().string());
        const bool sf = ext == ".sf2" || ext == ".sf3";
        if (ext != ".sfz" && !sf) {
            continue;
        }
        const fs::path rel = it->path().lexically_relative(root);
        SoundEntry e;
        e.path = it->path().string();
        e.library = rel.begin() != rel.end() ? rel.begin()->string() : std::string();
        if (e.library == rel.filename().string()) {
            e.library = "Loose sounds";
        }
        e.name = prettyName(it->path());
        e.isSoundFont = sf;
        e.family = guessFamily(lower(rel.generic_string()), sf);
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const SoundEntry& a, const SoundEntry& b) {
        if (a.family != b.family) return a.family < b.family;
        if (a.library != b.library) return a.library < b.library;
        return a.name < b.name;
    });
    return out;
}
