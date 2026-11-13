/*
 * Tape (Laurence Who Studio) — instruments
 * A sampler kit as a SoundFont 2 (.sf2), so Score (and any other SF2 player) can play it as an
 * instrument: one preset, one zone per pad on its key (C2–D#3), plus E3–C8 for a pad in keys mode.
 * Each pad's sound is decoded, trimmed to its start/end and stored as 16-bit mono.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "sf2export.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if __has_include(<QAudioDecoder>)
#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#define STUDIO_HAS_DECODER 1
#endif
#include <QEventLoop>
#include <QFile>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

using namespace au::instruments;

namespace {
struct Pcm {
    std::vector<float> mono;
    int rate = 44100;
};

quint32 le32(const char* p) { return quint32(quint8(p[0])) | quint32(quint8(p[1])) << 8 | quint32(quint8(p[2])) << 16 | quint32(quint8(p[3])) << 24; }
quint16 le16(const char* p) { return quint16(quint8(p[0]) | quint8(p[1]) << 8); }
quint32 be32(const char* p) { return quint32(quint8(p[3])) | quint32(quint8(p[2])) << 8 | quint32(quint8(p[1])) << 16 | quint32(quint8(p[0])) << 24; }
quint16 be16(const char* p) { return quint16(quint8(p[1]) | quint8(p[0]) << 8); }

// 80-bit IEEE extended (AIFF sample rate)
double ext80(const char* p)
{
    const int expon = ((quint8(p[0]) & 0x7F) << 8) | quint8(p[1]);
    quint64 mant = 0;
    for (int i = 0; i < 8; ++i) {
        mant = (mant << 8) | quint8(p[2 + i]);
    }
    if (expon == 0 && mant == 0) {
        return 0.0;
    }
    return std::ldexp(double(mant), expon - 16383 - 63);
}

float sampleAt(const char* p, int bits, bool isFloat, bool bigEndian)
{
    if (isFloat) {
        if (bits == 32) {
            quint32 u = bigEndian ? be32(p) : le32(p);
            float f;
            std::memcpy(&f, &u, 4);
            return f;
        }
        if (bits == 64) {
            quint64 u = 0;
            for (int i = 0; i < 8; ++i) {
                u |= quint64(quint8(p[bigEndian ? 7 - i : i])) << (8 * i);
            }
            double d;
            std::memcpy(&d, &u, 8);
            return float(d);
        }
        return 0.f;
    }
    switch (bits) {
    case 8:
        return bigEndian ? float(qint8(p[0])) / 128.f : (float(quint8(p[0])) - 128.f) / 128.f;
    case 16:
        return float(qint16(bigEndian ? be16(p) : le16(p))) / 32768.f;
    case 24: {
        qint32 v = bigEndian ? (quint8(p[0]) << 16 | quint8(p[1]) << 8 | quint8(p[2]))
                   : (quint8(p[2]) << 16 | quint8(p[1]) << 8 | quint8(p[0]));
        if (v & 0x800000) {
            v -= 0x1000000;
        }
        return float(v) / 8388608.f;
    }
    case 32:
        return float(qint32(bigEndian ? be32(p) : le32(p))) / 2147483648.f;
    }
    return 0.f;
}

void interleavedToMono(const char* data, qint64 bytes, int channels, int bits, bool isFloat, bool bigEndian, Pcm& out)
{
    const int bps = bits / 8;
    const int frameBytes = bps * channels;
    if (frameBytes <= 0) {
        return;
    }
    const qint64 frames = bytes / frameBytes;
    out.mono.resize(size_t(frames));
    for (qint64 f = 0; f < frames; ++f) {
        float s = 0.f;
        for (int c = 0; c < channels; ++c) {
            s += sampleAt(data + f * frameBytes + c * bps, bits, isFloat, bigEndian);
        }
        out.mono[size_t(f)] = s / float(channels);
    }
}

bool readWav(const QByteArray& b, Pcm& out)
{
    if (b.size() < 12 || std::memcmp(b.constData(), "RIFF", 4) != 0 || std::memcmp(b.constData() + 8, "WAVE", 4) != 0) {
        return false;
    }
    int channels = 0, bits = 0, fmt = 0;
    const char* data = nullptr;
    qint64 dataBytes = 0;
    qint64 pos = 12;
    while (pos + 8 <= b.size()) {
        const char* h = b.constData() + pos;
        const quint32 size = le32(h + 4);
        const char* body = h + 8;
        const qint64 avail = std::min<qint64>(size, b.size() - pos - 8);
        if (std::memcmp(h, "fmt ", 4) == 0 && avail >= 16) {
            fmt = le16(body);
            channels = le16(body + 2);
            out.rate = int(le32(body + 4));
            bits = le16(body + 14);
            if (fmt == 0xFFFE && avail >= 26) {
                fmt = le16(body + 24); // extensible: sub-format GUID starts with the format code
            }
        } else if (std::memcmp(h, "data", 4) == 0) {
            data = body;
            dataBytes = avail;
        }
        pos += 8 + qint64(size) + (size & 1);
    }
    if (!data || channels <= 0 || (fmt != 1 && fmt != 3)) {
        return false;
    }
    interleavedToMono(data, dataBytes, channels, bits, fmt == 3, false, out);
    return !out.mono.empty();
}

bool readAiff(const QByteArray& b, Pcm& out)
{
    if (b.size() < 12 || std::memcmp(b.constData(), "FORM", 4) != 0) {
        return false;
    }
    const bool aifc = std::memcmp(b.constData() + 8, "AIFC", 4) == 0;
    if (!aifc && std::memcmp(b.constData() + 8, "AIFF", 4) != 0) {
        return false;
    }
    int channels = 0, bits = 0;
    bool isFloat = false, bigEndian = true;
    const char* data = nullptr;
    qint64 dataBytes = 0;
    qint64 pos = 12;
    while (pos + 8 <= b.size()) {
        const char* h = b.constData() + pos;
        const quint32 size = be32(h + 4);
        const char* body = h + 8;
        const qint64 avail = std::min<qint64>(size, b.size() - pos - 8);
        if (std::memcmp(h, "COMM", 4) == 0 && avail >= 18) {
            channels = be16(body);
            bits = be16(body + 6);
            out.rate = int(std::lround(ext80(body + 8)));
            if (aifc && avail >= 22) {
                if (std::memcmp(body + 18, "sowt", 4) == 0) {
                    bigEndian = false;
                } else if (std::memcmp(body + 18, "fl32", 4) == 0 || std::memcmp(body + 18, "FL32", 4) == 0) {
                    isFloat = true;
                    bits = 32;
                } else if (std::memcmp(body + 18, "fl64", 4) == 0 || std::memcmp(body + 18, "FL64", 4) == 0) {
                    isFloat = true;
                    bits = 64;
                } else if (std::memcmp(body + 18, "NONE", 4) != 0) {
                    return false;
                }
            }
        } else if (std::memcmp(h, "SSND", 4) == 0 && avail >= 8) {
            const quint32 offset = be32(body);
            data = body + 8 + offset;
            dataBytes = avail - 8 - offset;
        }
        pos += 8 + qint64(size) + (size & 1);
    }
    if (!data || channels <= 0 || dataBytes <= 0) {
        return false;
    }
    bits = (bits + 7) / 8 * 8;
    interleavedToMono(data, dataBytes, channels, bits, isFloat, bigEndian, out);
    return !out.mono.empty();
}

// FLAC, OGG, MP3...: Qt Multimedia's decoder
bool readOther(const QString& path, Pcm& out)
{
#ifndef STUDIO_HAS_DECODER
    Q_UNUSED(path);
    Q_UNUSED(out);
    return false;
#else
    QAudioDecoder dec;
    QAudioFormat f;
    f.setSampleFormat(QAudioFormat::Float);
    f.setChannelCount(1);
    f.setSampleRate(48000);
    dec.setAudioFormat(f);
    dec.setSource(QUrl::fromLocalFile(path));
    QEventLoop loop;
    bool failed = false;
    QObject::connect(&dec, &QAudioDecoder::bufferReady, &loop, [&]() {
        const QAudioBuffer buf = dec.read();
        const QAudioFormat bf = buf.format();
        const int ch = std::max(1, bf.channelCount());
        out.rate = bf.sampleRate() > 0 ? bf.sampleRate() : out.rate;
        const qint64 frames = buf.frameCount();
        for (qint64 i = 0; i < frames; ++i) {
            float s = 0.f;
            for (int c = 0; c < ch; ++c) {
                const char* p = buf.constData<char>() + (i * ch + c) * bf.bytesPerSample();
                switch (bf.sampleFormat()) {
                case QAudioFormat::Float: s += sampleAt(p, 32, true, false);
                    break;
                case QAudioFormat::Int16: s += sampleAt(p, 16, false, false);
                    break;
                case QAudioFormat::Int32: s += sampleAt(p, 32, false, false);
                    break;
                case QAudioFormat::UInt8: s += sampleAt(p, 8, false, false);
                    break;
                default: break;
                }
            }
            out.mono.push_back(s / float(ch));
        }
    });
    QObject::connect(&dec, &QAudioDecoder::finished, &loop, &QEventLoop::quit);
    QObject::connect(&dec, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), &loop, [&]() { failed = true; loop.quit(); });
    QTimer::singleShot(60000, &loop, &QEventLoop::quit);
    dec.start();
    loop.exec();
    return !failed && !out.mono.empty();
#endif
}

bool decode(const QString& path, Pcm& out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray head = f.peek(12);
    if (head.startsWith("RIFF") || head.startsWith("FORM")) {
        const QByteArray b = f.readAll();
        if (readWav(b, out) || readAiff(b, out)) {
            return true;
        }
    }
    f.close();
    out = Pcm();
    return readOther(path, out);
}

// ---- SF2 writing

void put16(QByteArray& b, quint16 v) { b.append(char(v & 255)); b.append(char(v >> 8)); }
void put32(QByteArray& b, quint32 v) { put16(b, quint16(v & 0xFFFF)); put16(b, quint16(v >> 16)); }
void putName(QByteArray& b, const QString& name, int len)
{
    QByteArray n = name.toLatin1().left(len - 1);
    n.append(QByteArray(len - n.size(), '\0'));
    b.append(n);
}

QByteArray chunk(const char* id, const QByteArray& body)
{
    QByteArray c(id, 4);
    put32(c, quint32(body.size()));
    c.append(body);
    if (body.size() & 1) {
        c.append('\0');
    }
    return c;
}

QByteArray list(const char* type, const QByteArray& body)
{
    QByteArray b(type, 4);
    b.append(body);
    return chunk("LIST", b);
}

struct Gen {
    quint16 op;
    quint16 amount;
};

enum GenOp : quint16 {
    G_releaseVolEnv = 38, G_instrument = 41, G_keyRange = 43, G_sampleID = 53, G_sampleModes = 54,
    G_exclusiveClass = 57, G_overridingRootKey = 58
};

quint16 timecents(double seconds)
{
    const double tc = 1200.0 * std::log2(std::max(seconds, 0.001));
    return quint16(qint16(std::clamp(tc, -12000.0, 8000.0)));
}
}

bool au::instruments::writeKitSf2(const SamplerKit& kit, const QString& path, QString* error)
{
    struct Sample {
        QString name;
        quint32 start, end, loopStart, loopEnd;
        int rate;
    };
    QByteArray smpl;
    std::vector<Sample> samples;
    std::vector<std::vector<Gen> > zones;

    for (int i = 0; i < SAMPLER_PADS; ++i) {
        const SamplerPad& p = kit.pads[size_t(i)];
        if (p.isEmpty()) {
            continue;
        }
        Pcm pcm;
        if (!decode(p.file, pcm)) {
            continue;
        }
        const qint64 n = qint64(pcm.mono.size());
        const double a = std::clamp(std::min(p.start, p.end), 0.0, 1.0);
        const double b = std::clamp(std::max(p.start, p.end), 0.0, 1.0);
        qint64 from = std::clamp<qint64>(qint64(std::llround(a * double(n))), 0, n - 1);
        qint64 to = std::clamp<qint64>(qint64(std::llround(b * double(n))), from + 1, n);
        Sample s;
        s.name = QString("Pad %1").arg(i + 1);
        s.rate = pcm.rate;
        s.start = quint32(smpl.size() / 2);
        for (qint64 k = from; k < to; ++k) {
            const float v = std::clamp(pcm.mono[size_t(k)], -1.f, 1.f);
            put16(smpl, quint16(qint16(std::lround(v * 32767.f))));
        }
        s.end = quint32(smpl.size() / 2);
        s.loopStart = s.start;
        s.loopEnd = s.end;
        smpl.append(QByteArray(46 * 2, '\0'));
        const quint16 sampleId = quint16(samples.size());
        samples.push_back(s);

        const double seconds = double(to - from) / double(std::max(1, pcm.rate));
        std::vector<Gen> common;
        if (p.mode == PadMode::Loop) {
            common.push_back({ G_sampleModes, 1 });
        }
        common.push_back({ G_releaseVolEnv, timecents(p.mode == PadMode::OneShot ? seconds + 0.05 : 0.03) });
        if (p.choke) {
            common.push_back({ G_exclusiveClass, 1 });
        }
        const int key = SAMPLER_FIRST_KEY + i;
        std::vector<Gen> z { { G_keyRange, quint16(key | key << 8) }, { G_overridingRootKey, quint16(key) } };
        z.insert(z.end(), common.begin(), common.end());
        z.push_back({ G_sampleID, sampleId });
        zones.push_back(z);
        if (p.mode == PadMode::Keys) {
            std::vector<Gen> k { { G_keyRange, quint16(SAMPLER_KEYS_LO | SAMPLER_KEYS_HI << 8) },
                                 { G_overridingRootKey, quint16(SAMPLER_KEYS_CENTER) } };
            k.insert(k.end(), common.begin(), common.end());
            k.push_back({ G_sampleID, sampleId });
            zones.push_back(k);
        }
    }
    if (samples.empty()) {
        if (error) {
            *error = "No pad could be read";
        }
        return false;
    }

    const QString name = safeKitName(kit.name);
    // INFO
    QByteArray ifil;
    put16(ifil, 2);
    put16(ifil, 1);
    QByteArray nam = name.toLatin1().left(250);
    nam.append('\0');
    if (nam.size() & 1) {
        nam.append('\0');
    }
    QByteArray info = chunk("ifil", ifil) + chunk("isng", QByteArray("EMU8000\0", 8)) + chunk("INAM", nam)
                      + chunk("ISFT", QByteArray("Tape (Laurence Who Studio)\0\0", 28));

    // preset: one, bank 0 program 0, one zone pointing at instrument 0
    QByteArray phdr, pbag, pmod, pgen;
    putName(phdr, name, 20);
    put16(phdr, 0);
    put16(phdr, 0);
    put16(phdr, 0);
    put32(phdr, 0);
    put32(phdr, 0);
    put32(phdr, 0);
    putName(phdr, "EOP", 20);
    put16(phdr, 0);
    put16(phdr, 0);
    put16(phdr, 1);
    put32(phdr, 0);
    put32(phdr, 0);
    put32(phdr, 0);
    put16(pbag, 0);
    put16(pbag, 0);
    put16(pbag, 1);
    put16(pbag, 0);
    pmod.append(QByteArray(10, '\0'));
    put16(pgen, G_instrument);
    put16(pgen, 0);
    put16(pgen, 0);
    put16(pgen, 0);

    // instrument with the zones
    QByteArray inst, ibag, imod, igen;
    putName(inst, name, 20);
    put16(inst, 0);
    putName(inst, "EOI", 20);
    put16(inst, quint16(zones.size()));
    quint16 genIndex = 0;
    for (const auto& z : zones) {
        put16(ibag, genIndex);
        put16(ibag, 0);
        for (const Gen& g : z) {
            put16(igen, g.op);
            put16(igen, g.amount);
            ++genIndex;
        }
    }
    put16(ibag, genIndex);
    put16(ibag, 0);
    imod.append(QByteArray(10, '\0'));
    put32(igen, 0);

    QByteArray shdr;
    for (const Sample& s : samples) {
        putName(shdr, s.name, 20);
        put32(shdr, s.start);
        put32(shdr, s.end);
        put32(shdr, s.loopStart);
        put32(shdr, s.loopEnd);
        put32(shdr, quint32(s.rate));
        shdr.append(char(60));
        shdr.append(char(0));
        put16(shdr, 0);
        put16(shdr, 1);
    }
    putName(shdr, "EOS", 20);
    shdr.append(QByteArray(26, '\0'));

    const QByteArray pdta = chunk("phdr", phdr) + chunk("pbag", pbag) + chunk("pmod", pmod) + chunk("pgen", pgen)
                            + chunk("inst", inst) + chunk("ibag", ibag) + chunk("imod", imod) + chunk("igen", igen)
                            + chunk("shdr", shdr);
    QByteArray body("sfbk", 4);
    body += list("INFO", info);
    body += list("sdta", chunk("smpl", smpl));
    body += list("pdta", pdta);
    const QByteArray file = chunk("RIFF", body);

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(file) != file.size() || !out.commit()) {
        if (error) {
            *error = "Could not write " + path;
        }
        return false;
    }
    return true;
}
