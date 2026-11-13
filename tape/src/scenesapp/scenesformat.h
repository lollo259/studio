/*
 * Scenes (Laurence Who Studio) — a live sound switcher
 * The scenes set: what a .scenes.json file holds, the built-in voices and how a sound
 * is named so a set opens on another computer (path relative to the Sounds folder).
 * Also used by Tape (Record > Export to Scenes).
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <vector>

#include <QJsonObject>
#include <QString>

#include "../instruments/soundlibrary.h"

namespace scenes {
//! One sound of the library, or a built-in voice
struct SoundRef {
    QString id;      // "b:epiano", "sfz:vsco 2 ce/strings/violin.sfz", "sf2:generaluser gs/generaluser-gs.sf2#0:48"
    QString name;    // shown on the display
    QString library;
    QString family;
    QString path;    // absolute path (.sfz / .sf2); empty for built-ins
    bool soundFont = false;
    int bank = 0;
    int program = 0;

    bool isBuiltin() const { return id.startsWith("b:"); }
    bool isValid() const { return !id.isEmpty(); }
};

//! One sound on a range of keys. Same keys = stacked, different ranges = split.
struct Layer {
    SoundRef sound;
    int lo = 0;
    int hi = 127;
    double vol = 0.9;  // 0..1
    int tr = 0;        // semitones
};

struct Scene {
    QString name;
    std::vector<Layer> layers;
};

struct Learned {
    int type = 0; // 0x90 key, 0xB0 control; 0 = none
    int d1 = 0;
    bool isSet() const { return type != 0; }
};

struct SceneSet {
    QString name;
    int cur = 0;
    std::vector<Scene> scenes;
    Learned learnNext;
    Learned learnPrev;
};

struct Builtin {
    const char* id;
    const char* name;
    const char* family;
    const char* sfz;
};
const std::vector<Builtin>& builtins();
SoundRef builtinRef(const QString& id);

//! the sound's portable id, from a library entry
SoundRef refFor(const au::instruments::SoundEntry& e);
//! every sound: built-ins first, then the library
std::vector<SoundRef> allSounds();
//! finds the stored sound in this computer's library (id, then path, then name)
SoundRef resolve(const SoundRef& stored, const std::vector<SoundRef>& library);

QString noteName(int n);          // 60 -> "C4"
int parseNote(const QString& s);  // "C4", "c#3", "60" -> note; -1 if not a note

//! Documents/Studio/Scenes
QString scenesFolder();
QString setPath(const QString& setName);

QJsonObject soundToJson(const SoundRef& s);
SoundRef soundFromJson(const QJsonValue& v);
QJsonObject setToJson(const SceneSet& set);
//! reads a native set or the web app's export ({"scenes":[...]} with "s:" / "b:" ids)
bool setFromJson(const QByteArray& json, SceneSet& set, QString* error = nullptr);

bool saveSet(const SceneSet& set, const QString& path, QString* error = nullptr);
bool loadSet(const QString& path, SceneSet& set, QString* error = nullptr);
SceneSet defaultSet();
}
