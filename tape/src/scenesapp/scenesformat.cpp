/*
 * Scenes (Laurence Who Studio)
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "scenesformat.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include "../instruments/internal/samplerkit.h"

using namespace scenes;
namespace ai = au::instruments;

const std::vector<Builtin>& scenes::builtins()
{
    // the same five voices as the browser Scenes, made from sfizz's generators (no files needed)
    static const std::vector<Builtin> list = {
        { "b:epiano", "Electric piano", "Keys",
          "<global> ampeg_attack=0.004 ampeg_decay=1.8 ampeg_sustain=25 ampeg_release=0.35 amp_veltrack=80\n"
          "<region> sample=*sine volume=-6\n"
          "<region> sample=*sine transpose=12 volume=-18\n"
          "<region> sample=*triangle transpose=24 volume=-30\n" },
        { "b:pad", "Warm pad", "Synth",
          "<global> ampeg_attack=0.6 ampeg_sustain=100 ampeg_release=1.2 amp_veltrack=40\n"
          "<region> sample=*saw fil_type=lpf_2p cutoff=1800 volume=-17 tune=-6\n"
          "<region> sample=*saw fil_type=lpf_2p cutoff=1800 volume=-17 tune=6\n"
          "<region> sample=*triangle transpose=-12 volume=-14\n" },
        { "b:organ", "Organ", "Keys",
          "<global> ampeg_attack=0.01 ampeg_sustain=100 ampeg_release=0.08 amp_veltrack=20\n"
          "<region> sample=*sine volume=-9\n"
          "<region> sample=*sine transpose=12 volume=-13\n"
          "<region> sample=*sine transpose=19 tune=2 volume=-17\n"
          "<region> sample=*sine transpose=24 volume=-21\n"
          "<region> sample=*sine transpose=31 tune=2 volume=-25\n" },
        { "b:bass", "Synth bass", "Synth",
          "<global> ampeg_attack=0.002 ampeg_sustain=100 ampeg_release=0.12 amp_veltrack=70\n"
          "<region> sample=*saw fil_type=lpf_2p cutoff=500 fil_veltrack=2400 resonance=4 volume=-8\n"
          "<region> sample=*square transpose=-12 fil_type=lpf_2p cutoff=400 volume=-14\n" },
        { "b:bell", "Glass bells", "Keys",
          "<global> ampeg_attack=0.001 ampeg_decay=2.5 ampeg_sustain=0 ampeg_release=0.6 amp_veltrack=90\n"
          "<region> sample=*sine volume=-6\n"
          "<region> sample=*sine transpose=19 tune=2 volume=-14\n"
          "<region> sample=*sine transpose=29 tune=20 volume=-22 ampeg_decay=0.8\n" },
        { "b:sine", "Pure sine", "Synth",
          "<global> ampeg_attack=0.005 ampeg_release=0.2 amp_veltrack=90\n"
          "<region> sample=*sine volume=-8\n" },
    };
    return list;
}

SoundRef scenes::builtinRef(const QString& id)
{
    for (const Builtin& b : builtins()) {
        if (id == QLatin1String(b.id)) {
            SoundRef r;
            r.id = b.id;
            r.name = b.name;
            r.library = "Built-in";
            r.family = b.family;
            return r;
        }
    }
    return SoundRef();
}

static QString cleanPath(const QString& p)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(p));
}

SoundRef scenes::refFor(const ai::SoundEntry& e)
{
    // Tape's own built-in voices ("builtin:keys" ...) are Scenes' built-ins
    if (e.path.rfind("builtin:", 0) == 0) {
        const std::string t = e.path.substr(8);
        return builtinRef(t == "keys" ? "b:epiano" : t == "pad" ? "b:pad" : "b:sine");
    }
    SoundRef r;
    r.name = QString::fromStdString(e.name);
    r.library = QString::fromStdString(e.library);
    r.family = QString::fromUtf8(ai::familyName(e.family));
    r.path = cleanPath(QString::fromStdString(e.path));
    r.soundFont = e.isSoundFont;
    r.bank = std::max(0, e.bank);
    r.program = std::max(0, e.program);

    // relative to whichever Sounds folder holds it, so the set works on another computer too
    QString rel;
    std::vector<std::string> roots = ai::soundsFolders();
    for (const std::string& root : roots) {
        const QString base = cleanPath(QString::fromStdString(root)) + "/";
        if (r.path.startsWith(base, Qt::CaseInsensitive)) {
            rel = r.path.mid(base.size());
            break;
        }
    }
    if (rel.isEmpty()) {
        const QString kits = cleanPath(ai::samplerFolder()) + "/";
        rel = r.path.startsWith(kits, Qt::CaseInsensitive) ? "sampler kits/" + r.path.mid(kits.size()) : r.path;
    }
    rel = rel.toLower();
    r.id = (r.soundFont ? "sf2:" : "sfz:") + rel;
    if (r.soundFont) {
        r.id += QString("#%1:%2").arg(r.bank).arg(r.program);
    }
    return r;
}

std::vector<SoundRef> scenes::allSounds()
{
    std::vector<SoundRef> out;
    for (const Builtin& b : builtins()) {
        out.push_back(builtinRef(b.id));
    }
    for (const ai::SoundEntry& e : ai::scanAllSounds()) {
        out.push_back(refFor(e));
    }
    return out;
}

SoundRef scenes::resolve(const SoundRef& stored, const std::vector<SoundRef>& library)
{
    if (stored.id.isEmpty() && stored.name.isEmpty() && stored.path.isEmpty()) {
        return SoundRef();
    }
    if (stored.isBuiltin()) {
        SoundRef b = builtinRef(stored.id);
        return b.isValid() ? b : stored;
    }
    // Tape's own built-ins (Export to Scenes from a track playing one of them)
    if (stored.id.startsWith("builtin:") || stored.path.startsWith("builtin:")) {
        const QString t = (stored.id.startsWith("builtin:") ? stored.id : stored.path).mid(8);
        return builtinRef(t == "keys" ? "b:epiano" : t == "pad" ? "b:pad" : "b:sine");
    }
    for (const SoundRef& s : library) {
        if (s.id == stored.id) {
            return s;
        }
    }
    const QString p = cleanPath(stored.path);
    if (!p.isEmpty()) {
        for (const SoundRef& s : library) {
            if (s.path.compare(p, Qt::CaseInsensitive) == 0 && (!s.soundFont || (s.bank == stored.bank && s.program == stored.program))) {
                return s;
            }
        }
    }
    // the browser Scenes: "s:<picked folder>/<library>/.../x.sfz" (lowercase)
    if (stored.id.startsWith("s:")) {
        QString rel = stored.id.mid(2);
        const int slash = rel.indexOf('/');
        if (slash > 0) {
            rel = rel.mid(slash); // "/library/.../x.sfz"
        }
        for (const SoundRef& s : library) {
            if (!s.soundFont && s.path.toLower().endsWith(rel)) {
                return s;
            }
        }
    }
    // same file name in another folder, then the same name
    const QString file = QFileInfo(p.isEmpty() ? stored.id.section('#', 0, 0) : p).fileName().toLower();
    for (const SoundRef& s : library) {
        if (!file.isEmpty() && QFileInfo(s.path).fileName().toLower() == file && (!s.soundFont || s.program == stored.program)) {
            return s;
        }
    }
    for (const SoundRef& s : library) {
        if (!stored.name.isEmpty() && s.name.compare(stored.name, Qt::CaseInsensitive) == 0) {
            return s;
        }
    }
    // not on this computer: keep what the file says (it loads nothing, the display says so)
    SoundRef missing = stored;
    if (missing.name.isEmpty()) {
        missing.name = QFileInfo(stored.id.section('#', 0, 0)).completeBaseName();
    }
    return missing;
}

QString scenes::noteName(int n)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return QString("%1%2").arg(names[((n % 12) + 12) % 12]).arg(n / 12 - 1);
}

int scenes::parseNote(const QString& text)
{
    const QString s = text.trimmed();
    bool ok = false;
    const int v = s.toInt(&ok);
    if (ok) {
        return v >= 0 && v <= 127 ? v : -1;
    }
    static const QRegularExpression re("^([a-gA-G])([#b]?)(-?\\d)$");
    const QRegularExpressionMatch m = re.match(s);
    if (!m.hasMatch()) {
        return -1;
    }
    static const int base[] = { 9, 11, 0, 2, 4, 5, 7 }; // a b c d e f g
    int n = base[m.captured(1).toLower().at(0).unicode() - 'a'];
    n += m.captured(2) == "#" ? 1 : m.captured(2) == "b" ? -1 : 0;
    n += (m.captured(3).toInt() + 1) * 12;
    return n >= 0 && n <= 127 ? n : -1;
}

QString scenes::scenesFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Studio/Scenes";
}

QString scenes::setPath(const QString& setName)
{
    return scenesFolder() + "/" + ai::safeKitName(setName.isEmpty() ? QString("Scenes") : setName) + ".scenes.json";
}

QJsonObject scenes::soundToJson(const SoundRef& s)
{
    QJsonObject o { { "id", s.id }, { "name", s.name } };
    if (!s.library.isEmpty()) {
        o["library"] = s.library;
    }
    if (!s.path.isEmpty()) {
        o["path"] = QDir::toNativeSeparators(s.path);
    }
    if (s.soundFont) {
        o["soundfont"] = true;
        o["bank"] = s.bank;
        o["program"] = s.program;
    }
    return o;
}

SoundRef scenes::soundFromJson(const QJsonValue& v)
{
    SoundRef s;
    if (v.isString()) { // browser Scenes: just the id
        s.id = v.toString();
        return s;
    }
    const QJsonObject o = v.toObject();
    s.id = o.value("id").toString();
    s.name = o.value("name").toString();
    s.library = o.value("library").toString();
    s.path = cleanPath(o.value("path").toString());
    s.soundFont = o.value("soundfont").toBool();
    s.bank = o.value("bank").toInt();
    s.program = o.value("program").toInt();
    return s;
}

static QJsonValue learnedToJson(const Learned& l)
{
    return l.isSet() ? QJsonValue(QJsonObject { { "type", l.type }, { "d1", l.d1 } }) : QJsonValue();
}

static Learned learnedFromJson(const QJsonValue& v)
{
    Learned l;
    if (v.isObject()) {
        l.type = v.toObject().value("type").toInt();
        l.d1 = v.toObject().value("d1").toInt();
    }
    return l;
}

QJsonObject scenes::setToJson(const SceneSet& set)
{
    QJsonArray scenesArr;
    for (const Scene& sc : set.scenes) {
        QJsonArray layers;
        for (const Layer& l : sc.layers) {
            layers.append(QJsonObject { { "sound", soundToJson(l.sound) }, { "lo", l.lo }, { "hi", l.hi }, { "vol", l.vol }, { "tr", l.tr } });
        }
        scenesArr.append(QJsonObject { { "name", sc.name }, { "layers", layers } });
    }
    return QJsonObject {
        { "app", "Laurence Who Studio · Scenes" },
        { "version", 1 },
        { "name", set.name },
        { "cur", set.cur },
        { "scenes", scenesArr },
        { "learnNext", learnedToJson(set.learnNext) },
        { "learnPrev", learnedToJson(set.learnPrev) },
    };
}

bool scenes::setFromJson(const QByteArray& json, SceneSet& set, QString* error)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (!doc.isObject() || !doc.object().value("scenes").isArray()) {
        if (error) {
            *error = "This is not a scenes set.";
        }
        return false;
    }
    const QJsonObject o = doc.object();
    SceneSet s;
    s.name = o.value("name").toString();
    for (const QJsonValue& sv : o.value("scenes").toArray()) {
        const QJsonObject so = sv.toObject();
        Scene sc;
        sc.name = so.value("name").toString("Scene");
        for (const QJsonValue& lv : so.value("layers").toArray()) {
            const QJsonObject lo = lv.toObject();
            Layer l;
            l.sound = soundFromJson(lo.value("sound"));
            l.lo = std::clamp(lo.value("lo").toInt(0), 0, 127);
            l.hi = std::clamp(lo.value("hi").toInt(127), 0, 127);
            l.vol = std::clamp(lo.value("vol").toDouble(0.9), 0.0, 1.5);
            l.tr = std::clamp(lo.value("tr").toInt(0), -48, 48);
            if (l.lo > l.hi) {
                std::swap(l.lo, l.hi);
            }
            sc.layers.push_back(l);
        }
        s.scenes.push_back(sc);
    }
    s.cur = std::clamp(o.value("cur").toInt(0), 0, std::max(0, int(s.scenes.size()) - 1));
    s.learnNext = learnedFromJson(o.value("learnNext"));
    s.learnPrev = learnedFromJson(o.value("learnPrev"));
    set = s;
    return true;
}

bool scenes::saveSet(const SceneSet& set, const QString& path, QString* error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = "Could not write " + QDir::toNativeSeparators(path);
        }
        return false;
    }
    f.write(QJsonDocument(setToJson(set)).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error) {
            *error = "Could not write " + QDir::toNativeSeparators(path);
        }
        return false;
    }
    return true;
}

bool scenes::loadSet(const QString& path, SceneSet& set, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = "Could not open " + QDir::toNativeSeparators(path);
        }
        return false;
    }
    if (!setFromJson(f.readAll(), set, error)) {
        return false;
    }
    if (set.name.isEmpty()) {
        set.name = QFileInfo(path).fileName().section(".scenes", 0, 0);
    }
    return true;
}

SceneSet scenes::defaultSet()
{
    auto layer = [](const char* id, int lo, int hi, double vol, int tr) {
        Layer l;
        l.sound = builtinRef(id);
        l.lo = lo;
        l.hi = hi;
        l.vol = vol;
        l.tr = tr;
        return l;
    };
    SceneSet s;
    s.name = "My set";
    s.scenes.push_back({ "Piano", { layer("b:epiano", 0, 127, 0.9, 0) } });
    s.scenes.push_back({ "Pad + bells", { layer("b:pad", 0, 127, 0.6, 0), layer("b:bell", 72, 127, 0.5, 12) } });
    s.scenes.push_back({ "Split bass / organ", { layer("b:bass", 0, 54, 0.9, -12), layer("b:organ", 55, 127, 0.6, 0) } });
    return s;
}
