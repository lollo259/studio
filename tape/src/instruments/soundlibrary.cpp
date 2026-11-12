/*
 * Tape (Laurence Who Studio) — instruments
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "soundlibrary.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <QCoreApplication>

#include "internal/samplerkit.h"

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


// --- soundfont presets: read the RIFF "phdr" table (20-char name, preset, bank) ---
namespace {
struct Sf2Preset {
    std::string name;
    int program = 0;
    int bank = 0;
};

uint32_t rd32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
uint16_t rd16(const unsigned char* p) { return uint16_t(p[0] | (p[1] << 8)); }

std::vector<Sf2Preset> readSf2Presets(const fs::path& file)
{
    std::vector<Sf2Preset> out;
    std::ifstream f(file, std::ios::binary);
    if (!f) {
        return out;
    }
    unsigned char hdr[12];
    if (!f.read(reinterpret_cast<char*>(hdr), 12) || std::memcmp(hdr, "RIFF", 4) != 0 || std::memcmp(hdr + 8, "sfbk", 4) != 0) {
        return out;
    }
    // walk the top-level LIST chunks until "pdta", then its sub-chunks until "phdr"
    while (f) {
        unsigned char ch[12];
        if (!f.read(reinterpret_cast<char*>(ch), 8)) {
            break;
        }
        uint32_t size = rd32(ch + 4);
        if (std::memcmp(ch, "LIST", 4) == 0) {
            if (!f.read(reinterpret_cast<char*>(ch + 8), 4)) {
                break;
            }
            if (std::memcmp(ch + 8, "pdta", 4) != 0) {
                f.seekg(size - 4 + (size & 1), std::ios::cur);
                continue;
            }
            uint32_t left = size - 4;
            while (left >= 8) {
                unsigned char sh[8];
                if (!f.read(reinterpret_cast<char*>(sh), 8)) {
                    return out;
                }
                uint32_t ss = rd32(sh + 4);
                left -= 8;
                if (std::memcmp(sh, "phdr", 4) == 0) {
                    std::vector<unsigned char> buf(ss);
                    if (!f.read(reinterpret_cast<char*>(buf.data()), ss)) {
                        return out;
                    }
                    for (uint32_t i = 0; i + 38 <= ss; i += 38) {
                        const unsigned char* r = buf.data() + i;
                        std::string name(reinterpret_cast<const char*>(r), strnlen(reinterpret_cast<const char*>(r), 20));
                        if (name == "EOP") {
                            break;
                        }
                        Sf2Preset p;
                        p.name = name;
                        p.program = rd16(r + 20);
                        p.bank = rd16(r + 22);
                        out.push_back(p);
                    }
                    return out;
                }
                f.seekg(ss + (ss & 1), std::ios::cur);
                left -= std::min(left, ss + (ss & 1));
            }
            return out;
        }
        f.seekg(size + (size & 1), std::ios::cur);
    }
    return out;
}

// General MIDI families by program number (bank 128 is the drum kits)
au::instruments::SoundFamily gmFamily(int bank, int program)
{
    using F = au::instruments::SoundFamily;
    if (bank >= 120) return F::Percussion;
    if (program < 8) return F::Piano;
    if (program < 24) return F::Keys;
    if (program < 56) return F::Strings; // guitars, basses, strings, ensembles, choirs (52-54 below)
    if (program < 64) return F::Brass;
    if (program < 80) return F::Winds;
    if (program < 104) return F::Synth;
    if (program >= 112 && program < 120) return F::Percussion;
    return F::Other;
}
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
        if (sf) {
            // one entry per program in the soundfont, filed under the soundfont's name
            e.library = prettyName(it->path());
            std::vector<Sf2Preset> presets = readSf2Presets(it->path());
            std::sort(presets.begin(), presets.end(), [](const Sf2Preset& a, const Sf2Preset& b) {
                return a.bank != b.bank ? a.bank < b.bank : a.program < b.program;
            });
            for (const Sf2Preset& p : presets) {
                SoundEntry pe = e;
                pe.name = p.name;
                pe.bank = p.bank;
                pe.program = p.program;
                pe.family = (p.name.find("Choir") != std::string::npos || p.name.find("Voice") != std::string::npos
                             || p.name.find("Aah") != std::string::npos || p.name.find("Ooh") != std::string::npos)
                            ? SoundFamily::Voice : gmFamily(p.bank, p.program);
                out.push_back(std::move(pe));
            }
            if (presets.empty()) {
                e.bank = 0;
                e.program = 0;
                out.push_back(std::move(e));
            }
            continue;
        }
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const SoundEntry& a, const SoundEntry& b) {
        if (a.family != b.family) return a.family < b.family;
        if (a.library != b.library) return a.library < b.library;
        return a.name < b.name;
    });
    return out;
}

std::vector<std::string> au::instruments::soundsFolders()
{
    std::vector<std::string> out { defaultSoundsFolder() };
    std::error_code ec;
    // Studio download: Studio/Tape/bin/Tape.exe and Studio/Sounds
    const fs::path shipped = fs::path(QCoreApplication::applicationDirPath().toStdWString()) / ".." / ".." / "Sounds";
    if (fs::is_directory(shipped, ec)) {
        const fs::path canon = fs::weakly_canonical(shipped, ec);
        if (fs::weakly_canonical(fs::path(out.front()), ec) != canon) {
            out.push_back(canon.string());
        }
    }
    return out;
}

std::vector<SoundEntry> au::instruments::scanAllSounds()
{
    std::vector<SoundEntry> all;
    std::vector<std::string> seenLibraries;
    for (const std::string& folder : soundsFolders()) {
        std::vector<SoundEntry> found = scanSounds(folder);
        std::vector<std::string> here;
        for (SoundEntry& e : found) {
            // Sounds/Sampler holds copies of the Sampler's kits for Scenes: Tape lists the kits below
            if (e.library == "Sampler") {
                continue;
            }
            // the same library copied in both places is listed once (the user's copy wins)
            if (std::find(seenLibraries.begin(), seenLibraries.end(), e.library) != seenLibraries.end()) {
                continue;
            }
            if (std::find(here.begin(), here.end(), e.library) == here.end()) {
                here.push_back(e.library);
            }
            all.push_back(std::move(e));
        }
        seenLibraries.insert(seenLibraries.end(), here.begin(), here.end());
    }
    // the Sampler's kits (Documents/Studio/Sampler) are ordinary .sfz instruments too
    exportKitsToSounds();
    for (const QString& kit : kitNames()) {
        SoundEntry e;
        e.name = kit.toStdString();
        e.library = "Sampler";
        e.path = kitSfzPath(kit).toStdString();
        e.family = SoundFamily::Percussion;
        std::error_code ec;
        if (fs::is_regular_file(fs::path(kitSfzPath(kit).toStdWString()), ec)) {
            all.push_back(std::move(e));
        }
    }
    std::stable_sort(all.begin(), all.end(), [](const SoundEntry& a, const SoundEntry& b) {
        if (a.family != b.family) return a.family < b.family;
        if (a.library != b.library) return a.library < b.library;
        return false;
    });
    return all;
}
