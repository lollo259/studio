/*
 * Tape (Laurence Who Studio) — instruments
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "samplerkit.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

using namespace au::instruments;

namespace {
uint32_t le32(const unsigned char* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

uint16_t le16(const unsigned char* p)
{
    return uint16_t(p[0] | (p[1] << 8));
}

float sampleAt(const unsigned char* p, int bits, bool isFloat)
{
    if (isFloat) {
        if (bits == 32) {
            float f;
            std::memcpy(&f, p, 4);
            return std::isfinite(f) ? f : 0.0f;
        }
        double d;
        std::memcpy(&d, p, 8);
        return std::isfinite(d) ? float(d) : 0.0f;
    }
    switch (bits) {
    case 8:
        return (int(p[0]) - 128) / 128.0f;
    case 16:
        return int16_t(le16(p)) / 32768.0f;
    case 24: {
        int32_t v = int32_t(uint32_t(p[0]) << 8 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 24) >> 8;
        return float(v) / 8388608.0f;
    }
    case 32:
        return float(int32_t(le32(p))) / 2147483648.0f;
    default:
        return 0.0f;
    }
}
}

WavPeaks au::instruments::readWavPeaks(const QString& path, int pairs)
{
    WavPeaks out;
    QFile f(path);
    if (pairs <= 0 || !f.open(QIODevice::ReadOnly)) {
        return out;
    }
    unsigned char hdr[12];
    if (f.read(reinterpret_cast<char*>(hdr), 12) != 12 || std::memcmp(hdr, "RIFF", 4) != 0 || std::memcmp(hdr + 8, "WAVE", 4) != 0) {
        return out;
    }

    int format = 0, channels = 0, bits = 0, blockAlign = 0, rate = 0;
    qint64 dataPos = -1, dataSize = 0;
    while (!f.atEnd()) {
        unsigned char ch[8];
        if (f.read(reinterpret_cast<char*>(ch), 8) != 8) {
            break;
        }
        const qint64 size = le32(ch + 4);
        const qint64 body = f.pos();
        if (std::memcmp(ch, "fmt ", 4) == 0 && size >= 16) {
            unsigned char fmt[40] = {};
            const qint64 n = f.read(reinterpret_cast<char*>(fmt), std::min<qint64>(size, 40));
            if (n < 16) {
                return out;
            }
            format = le16(fmt);
            channels = le16(fmt + 2);
            rate = int(le32(fmt + 4));
            blockAlign = le16(fmt + 12);
            bits = le16(fmt + 14);
            if (format == 0xFFFE && n >= 26) {
                format = le16(fmt + 24); // WAVE_FORMAT_EXTENSIBLE: the sub-format GUID starts with the real tag
            }
        } else if (std::memcmp(ch, "data", 4) == 0) {
            dataPos = body;
            dataSize = size;
            // a streaming writer may leave the size at 0 or 0xFFFFFFFF: use what is in the file
            if (dataSize == 0 || dataSize == 0xFFFFFFFFLL || body + dataSize > f.size()) {
                dataSize = f.size() - body;
            }
            break;
        }
        if (!f.seek(body + size + (size & 1))) {
            break;
        }
    }

    const bool isFloat = format == 3;
    if (dataPos < 0 || channels <= 0 || rate <= 0 || (format != 1 && !isFloat)) {
        return out;
    }
    if (isFloat ? (bits != 32 && bits != 64) : (bits != 8 && bits != 16 && bits != 24 && bits != 32)) {
        return out;
    }
    const int bytesPerSample = bits / 8;
    if (blockAlign < bytesPerSample * channels) {
        blockAlign = bytesPerSample * channels;
    }

    out.frames = dataSize / blockAlign;
    out.sampleRate = rate;
    out.channels = channels;
    out.peaks.assign(size_t(pairs) * 2, 0.0f);
    if (out.frames <= 0) {
        out.ok = true;
        return out;
    }

    std::vector<float> mins(size_t(pairs), 1.0f), maxs(size_t(pairs), -1.0f);
    f.seek(dataPos);
    const qint64 framesPerRead = 16384;
    QByteArray buf;
    qint64 frame = 0;
    while (frame < out.frames) {
        const qint64 want = std::min(framesPerRead, out.frames - frame);
        buf = f.read(want * blockAlign);
        const qint64 got = buf.size() / blockAlign;
        if (got <= 0) {
            break;
        }
        const auto* p = reinterpret_cast<const unsigned char*>(buf.constData());
        for (qint64 i = 0; i < got; ++i, ++frame) {
            const unsigned char* fp = p + i * blockAlign;
            float sum = 0.0f;
            for (int c = 0; c < channels; ++c) {
                sum += sampleAt(fp + c * bytesPerSample, bits, isFloat);
            }
            const float v = sum / float(channels);
            const size_t b = size_t(frame * pairs / out.frames);
            mins[b] = std::min(mins[b], v);
            maxs[b] = std::max(maxs[b], v);
        }
    }
    for (int i = 0; i < pairs; ++i) {
        const bool any = maxs[size_t(i)] >= mins[size_t(i)];
        out.peaks[size_t(i) * 2] = any ? std::clamp(mins[size_t(i)], -1.0f, 1.0f) : 0.0f;
        out.peaks[size_t(i) * 2 + 1] = any ? std::clamp(maxs[size_t(i)], -1.0f, 1.0f) : 0.0f;
    }
    out.ok = true;
    return out;
}

const char* au::instruments::padModeId(PadMode m)
{
    switch (m) {
    case PadMode::OneShot: return "one-shot";
    case PadMode::Gate: return "gate";
    case PadMode::Loop: return "loop";
    case PadMode::Keys: return "keys";
    }
    return "one-shot";
}

PadMode au::instruments::padModeFromId(const QString& id)
{
    if (id == "gate") {
        return PadMode::Gate;
    }
    if (id == "loop") {
        return PadMode::Loop;
    }
    if (id == "keys") {
        return PadMode::Keys;
    }
    return PadMode::OneShot;
}

QString au::instruments::samplerFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Studio/Sampler";
}

QString au::instruments::safeKitName(const QString& name)
{
    QString s;
    for (const QChar c : name.trimmed()) {
        if (c.unicode() < 32 || QString("\\/:*?\"<>|").contains(c)) {
            continue;
        }
        s += c;
    }
    while (s.endsWith('.') || s.endsWith(' ')) {
        s.chop(1);
    }
    return s.isEmpty() ? QString("Kit 1") : s.left(60);
}

QString au::instruments::kitSfzPath(const QString& kitName)
{
    return samplerFolder() + "/" + safeKitName(kitName) + ".sfz";
}

QString au::instruments::kitJsonPath(const QString& kitName)
{
    return samplerFolder() + "/" + safeKitName(kitName) + ".studiokit.json";
}

QStringList au::instruments::kitNames()
{
    QStringList names;
    const QStringList files = QDir(samplerFolder()).entryList({ "*.studiokit.json" }, QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QString& f : files) {
        names << f.left(f.size() - int(qstrlen(".studiokit.json")));
    }
    return names;
}

QString au::instruments::makeSfz(const SamplerKit& kit)
{
    QString s;
    s += "// Tape sampler kit: " + safeKitName(kit.name) + "\n";
    s += "// Written by Tape; edit the kit in Record > Sampler (changes here are overwritten).\n";
    s += "// Pads 1-16 play on keys 36-51 (C2-D#3); a pad in keys mode also plays from E3 up.\n\n";

    for (int i = 0; i < SAMPLER_PADS; ++i) {
        const SamplerPad& p = kit.pads[size_t(i)];
        if (p.isEmpty()) {
            continue;
        }
        QString path = p.file;
        path.replace('\\', '/');

        QString common;
        if (p.frames > 0) {
            const double a = std::clamp(std::min(p.start, p.end), 0.0, 1.0);
            const double b = std::clamp(std::max(p.start, p.end), 0.0, 1.0);
            qint64 offset = qint64(std::llround(a * double(p.frames)));
            qint64 last = qint64(std::llround(b * double(p.frames))) - 1;
            offset = std::clamp<qint64>(offset, 0, p.frames - 1);
            last = std::clamp<qint64>(last, offset, p.frames - 1);
            if (offset > 0) {
                common += QString("offset=%1\n").arg(offset);
            }
            if (last < p.frames - 1 || p.mode == PadMode::Loop) {
                common += QString("end=%1\n").arg(last);
            }
            if (p.mode == PadMode::Loop) {
                common += QString("loop_start=%1\nloop_end=%2\n").arg(offset).arg(last);
            }
        }
        switch (p.mode) {
        case PadMode::OneShot:
            common += "loop_mode=one_shot\n";
            break;
        case PadMode::Gate:
        case PadMode::Keys:
            common += "loop_mode=no_loop\nampeg_release=0.03\n";
            break;
        case PadMode::Loop:
            common += "loop_mode=loop_continuous\nampeg_release=0.03\n";
            break;
        }
        if (p.choke) {
            common += "group=1\noff_by=1\noff_mode=fast\n";
        }
        common += "sample=" + path + "\n";

        const int key = SAMPLER_FIRST_KEY + i;
        QString label = p.name;
        label.replace('\n', ' ').replace('\r', ' ');
        s += QString("// pad %1: %2\n").arg(i + 1).arg(label);
        s += QString("<region>\nkey=%1\npitch_keycenter=%1\n").arg(key) + common + "\n";
        if (p.mode == PadMode::Keys) {
            s += QString("<region>\nlokey=%1\nhikey=%2\npitch_keycenter=%3\n")
                 .arg(SAMPLER_KEYS_LO).arg(SAMPLER_KEYS_HI).arg(SAMPLER_KEYS_CENTER) + common + "\n";
        }
    }
    return s;
}

QByteArray au::instruments::kitToJson(const SamplerKit& kit)
{
    QJsonArray pads;
    for (const SamplerPad& p : kit.pads) {
        QJsonObject o;
        o["name"] = p.name;
        o["file"] = p.file;
        o["start"] = p.start;
        o["end"] = p.end;
        o["mode"] = QString::fromLatin1(padModeId(p.mode));
        o["choke"] = p.choke;
        o["frames"] = double(p.frames);
        pads.append(o);
    }
    QJsonObject root;
    root["format"] = "studio-sampler-kit";
    root["version"] = 1;
    root["name"] = kit.name;
    root["pads"] = pads;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool au::instruments::kitFromJson(const QByteArray& json, SamplerKit& kit)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    const QJsonObject root = doc.object();
    kit = SamplerKit();
    kit.name = root.value("name").toString();
    const QJsonArray pads = root.value("pads").toArray();
    for (int i = 0; i < SAMPLER_PADS && i < pads.size(); ++i) {
        const QJsonObject o = pads.at(i).toObject();
        SamplerPad& p = kit.pads[size_t(i)];
        p.name = o.value("name").toString();
        p.file = o.value("file").toString();
        p.start = std::clamp(o.value("start").toDouble(0.0), 0.0, 1.0);
        p.end = std::clamp(o.value("end").toDouble(1.0), 0.0, 1.0);
        p.mode = padModeFromId(o.value("mode").toString());
        p.choke = o.value("choke").toBool(false);
        p.frames = qint64(o.value("frames").toDouble(0.0));
    }
    return true;
}

bool au::instruments::saveKit(const SamplerKit& kit, QString* error)
{
    if (!QDir().mkpath(samplerFolder())) {
        if (error) {
            *error = "Could not create " + samplerFolder();
        }
        return false;
    }
    const bool empty = std::all_of(kit.pads.begin(), kit.pads.end(), [](const SamplerPad& p) { return p.isEmpty(); });
    if (empty) {
        // nothing to play: no .sfz (an SFZ without regions would not load), only the kit state
        QFile::remove(kitSfzPath(kit.name));
    } else {
        QSaveFile sfz(kitSfzPath(kit.name));
        if (!sfz.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (error) {
                *error = "Could not write " + sfz.fileName();
            }
            return false;
        }
        sfz.write(makeSfz(kit).toUtf8());
        if (!sfz.commit()) {
            if (error) {
                *error = "Could not write " + sfz.fileName();
            }
            return false;
        }
    }
    QSaveFile js(kitJsonPath(kit.name));
    if (!js.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = "Could not write " + js.fileName();
        }
        return false;
    }
    js.write(kitToJson(kit));
    if (!js.commit()) {
        if (error) {
            *error = "Could not write " + js.fileName();
        }
        return false;
    }
    return true;
}

bool au::instruments::loadKit(const QString& kitName, SamplerKit& kit)
{
    QFile f(kitJsonPath(kitName));
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    if (!kitFromJson(f.readAll(), kit)) {
        return false;
    }
    kit.name = safeKitName(kitName);
    return true;
}

void au::instruments::removeKitFiles(const QString& kitName)
{
    QFile::remove(kitSfzPath(kitName));
    QFile::remove(kitJsonPath(kitName));
}
