/*
 * Score (Laurence Who Studio): the click track sent to Tape with the instruments
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include <QByteArray>
#include <QString>
#include <QSaveFile>

#include "engraving/dom/instrument.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/part.h"
#include "engraving/dom/repeatlist.h"
#include "engraving/dom/score.h"
#include "engraving/dom/sig.h"

namespace mu::playback::studioclick_detail {
constexpr int RATE = 48000;

inline void put16(QByteArray& b, quint16 v) { b.append(char(v & 255)); b.append(char(v >> 8)); }
inline void put32(QByteArray& b, quint32 v) { put16(b, quint16(v & 0xFFFF)); put16(b, quint16(v >> 16)); }

inline void addClick(std::vector<float>& out, double seconds, bool accent)
{
    const size_t at = size_t(std::max(0.0, seconds) * RATE);
    const double freq = accent ? 1760.0 : 1175.0;
    const double gain = accent ? 0.9 : 0.6;
    const size_t len = size_t(0.045 * RATE);
    if (out.size() < at + len) {
        out.resize(at + len, 0.f);
    }
    for (size_t i = 0; i < len; ++i) {
        const double t = double(i) / RATE;
        const double env = std::exp(-t * 90.0);
        out[at + i] += float(gain * env * std::sin(2.0 * 3.14159265358979 * freq * t));
    }
}
}

namespace mu::playback {
//! Writes a mono 48 kHz WAV with a click on every beat (accented on the first beat of each bar),
//! following the score's tempo, tempo changes and repeats.
inline bool writeClickTrack(const mu::engraving::Score* score, const QString& path)
{
    using namespace mu::engraving;
    using namespace mu::playback::studioclick_detail;
    if (!score) {
        return false;
    }
    std::vector<float> pcm;
    const RepeatList& repeats = score->repeatList();
    for (const RepeatSegment* rs : repeats) {
        const int offset = rs->utick - rs->tick;
        for (const Measure* m = score->tick2measure(Fraction::fromTicks(rs->tick)); m && m->tick().ticks() < rs->tick + rs->len();
             m = m->nextMeasure()) {
            const TimeSigFrac ts = score->sigmap()->timesig(m->tick().ticks()).timesig();
            const int num = std::max(1, ts.numerator());
            const int den = std::max(1, ts.denominator());
            // compound metres (6/8, 9/8, 12/8...) click on the dotted beat
            const bool compound = den >= 8 && num % 3 == 0 && num > 3;
            const int beatTicks = compound ? (Constants::DIVISION * 4 / den) * 3 : Constants::DIVISION * 4 / den;
            const int startTick = m->tick().ticks();
            const int endTick = startTick + m->ticks().ticks();
            int beat = 0;
            for (int t = startTick; t < endTick; t += std::max(1, beatTicks), ++beat) {
                addClick(pcm, score->utick2utime(t + offset), beat == 0);
            }
        }
    }
    if (pcm.empty()) {
        return false;
    }
    pcm.resize(pcm.size() + RATE / 2, 0.f);

    QByteArray b;
    const quint32 dataBytes = quint32(pcm.size() * 2);
    b += "RIFF";
    put32(b, 36 + dataBytes);
    b += "WAVEfmt ";
    put32(b, 16);
    put16(b, 1);
    put16(b, 1);
    put32(b, RATE);
    put32(b, RATE * 2);
    put16(b, 2);
    put16(b, 16);
    b += "data";
    put32(b, dataBytes);
    for (float s : pcm) {
        put16(b, quint16(qint16(std::lround(std::clamp(s, -1.f, 1.f) * 32767.f))));
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    f.write(b);
    return f.commit();
}
}

namespace mu::playback {
//! Studio: the Tape instrument's own part makes no sound - it isn't rendered into Tape
inline bool isStudioTapeOnly(const mu::engraving::Score* score)
{
    if (!score || score->parts().empty()) {
        return false;
    }
    for (const mu::engraving::Part* p : score->parts()) {
        const mu::engraving::Instrument* in = p->instrument();
        if (!in || in->id() != u"tape") {
            return false;
        }
    }
    return true;
}
}
