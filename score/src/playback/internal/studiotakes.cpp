/*
 * Score (Laurence Who Studio): a take played in Tape written into the score as notes.
 * The take's MIDI (Tape writes it at 960 ticks per quarter, 120 bpm, so 1 s = 1920 ticks) is
 * placed on the score's own timeline (where the take started in Tape), turned into a MIDI file
 * on the score's grid and bar lines, imported with Score's MIDI import (which quantizes it), and
 * pasted into the chosen instrument from the bar where the take starts. One undo step.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "studiolink.h"

#include <algorithm>
#include <initializer_list>
#include <map>
#include <vector>

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMimeData>
#include <QStandardPaths>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/part.h"
#include "engraving/dom/sig.h"
#include "engraving/dom/staff.h"
#include "notation/imasternotation.h"
#include "notation/inotation.h"
#include "notation/inotationinteraction.h"
#include "notation/inotationplayback.h"
#include "notation/inotationselection.h"
#include "project/inotationproject.h"

using namespace mu::playback;
using namespace mu::engraving;

namespace {
struct TakeNote {
    double on = 0.0, off = 0.0;
    int pitch = 60, velo = 80;
};

quint32 be32(const char* p) { return quint32(quint8(p[0])) << 24 | quint32(quint8(p[1])) << 16 | quint32(quint8(p[2])) << 8 | quint8(p[3]); }

std::vector<TakeNote> readTake(const QString& path)
{
    std::vector<TakeNote> notes;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return notes;
    }
    const QByteArray b = f.readAll();
    if (b.size() < 22 || !b.startsWith("MThd")) {
        return notes;
    }
    const int division = (quint8(b[12]) << 8) | quint8(b[13]);
    int pos = 8 + int(be32(b.constData() + 4));
    double secPerTick = 0.5 / std::max(1, division); // 120 bpm until a tempo event says otherwise
    std::map<int, TakeNote> open;
    while (pos + 8 <= b.size()) {
        const int len = int(be32(b.constData() + pos + 4));
        const bool isTrack = b.mid(pos, 4) == "MTrk";
        int p = pos + 8;
        const int end = std::min<int>(b.size(), p + len);
        pos = p + len;
        if (!isTrack) {
            continue;
        }
        double t = 0.0;
        quint8 status = 0;
        while (p < end) {
            quint32 delta = 0;
            for (int i = 0; i < 4 && p < end; ++i) {
                const quint8 c = quint8(b[p++]);
                delta = (delta << 7) | (c & 0x7F);
                if (!(c & 0x80)) {
                    break;
                }
            }
            t += delta * secPerTick;
            if (p >= end) {
                break;
            }
            quint8 c = quint8(b[p]);
            if (c & 0x80) {
                status = c;
                ++p;
            }
            if (status == 0xFF) {
                const quint8 type = quint8(b[p++]);
                quint32 l = 0;
                for (int i = 0; i < 4 && p < end; ++i) {
                    const quint8 d = quint8(b[p++]);
                    l = (l << 7) | (d & 0x7F);
                    if (!(d & 0x80)) {
                        break;
                    }
                }
                if (type == 0x51 && l == 3 && p + 3 <= end) {
                    const int us = (quint8(b[p]) << 16) | (quint8(b[p + 1]) << 8) | quint8(b[p + 2]);
                    secPerTick = us / 1e6 / std::max(1, division);
                }
                p += int(l);
                continue;
            }
            if (status == 0xF0 || status == 0xF7) {
                quint32 l = 0;
                for (int i = 0; i < 4 && p < end; ++i) {
                    const quint8 d = quint8(b[p++]);
                    l = (l << 7) | (d & 0x7F);
                    if (!(d & 0x80)) {
                        break;
                    }
                }
                p += int(l);
                continue;
            }
            const int kind = status & 0xF0;
            const int nData = (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
            if (p + nData > end) {
                break;
            }
            const int d1 = quint8(b[p]);
            const int d2 = nData > 1 ? quint8(b[p + 1]) : 0;
            p += nData;
            if (kind == 0x90 && d2 > 0) {
                TakeNote n;
                n.on = t;
                n.pitch = d1;
                n.velo = d2;
                open[d1] = n;
            } else if (kind == 0x80 || (kind == 0x90 && d2 == 0)) {
                auto it = open.find(d1);
                if (it != open.end()) {
                    it->second.off = std::max(t, it->second.on + 0.02);
                    notes.push_back(it->second);
                    open.erase(it);
                }
            }
        }
    }
    for (auto& [pitch, n] : open) {
        n.off = n.on + 0.5;
        notes.push_back(n);
    }
    std::sort(notes.begin(), notes.end(), [](const TakeNote& a, const TakeNote& c) { return a.on < c.on; });
    return notes;
}

QByteArray bytes(std::initializer_list<int> v)
{
    QByteArray b;
    for (int x : v) {
        b.append(char(x));
    }
    return b;
}

void varLen(QByteArray& b, quint32 v)
{
    quint8 buf[5];
    int n = 0;
    buf[n++] = v & 0x7F;
    while ((v >>= 7)) {
        buf[n++] = quint8(0x80 | (v & 0x7F));
    }
    while (n--) {
        b.append(char(buf[n]));
    }
}
}

bool StudioLink::writeTakeIntoScore(const QString& midiPath, double atSecs, const QString& partName, QString* error)
{
    auto fail = [error](const QString& e) { if (error) { *error = e; } return false; };

    auto master = projectSession()->masterNotation();
    MasterScore* score = master ? master->masterScore() : nullptr;
    if (!score || !master->playback()) {
        return fail("No score is open");
    }
    const std::vector<TakeNote> take = readTake(midiPath);
    if (take.empty()) {
        return fail("The take has no notes");
    }

    // the instrument
    Part* part = nullptr;
    for (Part* p : score->parts()) {
        const QString name = QString::fromStdString(muse::io::escapeFileName(muse::io::path_t(p->partName().toStdString())).toStdString());
        if (name == partName || p->partName().toQString() == partName) {
            part = p;
            break;
        }
    }
    if (!part) {
        part = score->parts().empty() ? nullptr : score->parts().front();
    }
    if (!part || part->nstaves() == 0) {
        return fail("The score has no instruments");
    }
    const staff_idx_t staffIdx = part->staff(0)->idx();
    const bool twoStaves = part->nstaves() > 1;

    // seconds -> score ticks
    auto toTick = [&](double s) { return int(master->playback()->secToTick(muse::audio::secs_t(std::max(0.0, s)))); };
    const int firstTick = toTick(atSecs + take.front().on);
    Measure* startMeasure = score->tick2measure(Fraction::fromTicks(firstTick));
    if (!startMeasure) {
        return fail("The take starts after the end of the score");
    }
    const int origin = startMeasure->tick().ticks();

    // a MIDI file on the score's grid, starting at that bar (480 ticks per quarter like the score)
    struct Ev {
        int tick;
        QByteArray bytes;
    };
    std::vector<Ev> evs;
    int lastTick = 0;
    for (const TakeNote& n : take) {
        const int on = toTick(atSecs + n.on) - origin;
        const int off = std::max(on + 30, toTick(atSecs + n.off) - origin);
        if (on < 0) {
            continue;
        }
        evs.push_back({ on, bytes({ 0x90, n.pitch, std::clamp(n.velo, 1, 127) }) });
        evs.push_back({ off, bytes({ 0x80, n.pitch, 0 }) });
        lastTick = std::max(lastTick, off);
    }
    // time signatures from that bar to the end of the take
    for (Measure* m = startMeasure; m && m->tick().ticks() - origin <= lastTick; m = m->nextMeasure()) {
        const TimeSigFrac ts = m->timesig();
        if (m == startMeasure || !(m->prevMeasure() && m->prevMeasure()->timesig() == ts)) {
            int d = 0;
            for (int v = ts.denominator(); v > 1; v >>= 1) {
                ++d;
            }
            evs.push_back({ m->tick().ticks() - origin, bytes({ 0xFF, 0x58, 4, ts.numerator(), d, 24, 8 }) });
        }
    }
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& c) {
        if (a.tick != c.tick) {
            return a.tick < c.tick;
        }
        return quint8(a.bytes[0]) == 0xFF && quint8(c.bytes[0]) != 0xFF; // meta first
    });
    QByteArray track;
    varLen(track, 0);
    track.append(QByteArray::fromHex("FF510307A120")); // 120 bpm (the grid is what counts)
    varLen(track, 0);
    track.append(char(0xC0));
    track.append(char(twoStaves ? 0 : 40)); // piano splits into two staves; anything else stays on one
    int prev = 0;
    for (const Ev& e : evs) {
        varLen(track, quint32(e.tick - prev));
        prev = e.tick;
        track.append(e.bytes);
    }
    varLen(track, 0);
    track.append(QByteArray::fromHex("FF2F00"));
    QByteArray file("MThd\0\0\0\x06\0\0\0\x01\x01\xE0", 14);
    file.append("MTrk");
    const quint32 len = quint32(track.size());
    file.append(char(len >> 24)).append(char(len >> 16)).append(char(len >> 8)).append(char(len));
    file.append(track);
    const QString tmpPath = QDir::tempPath() + "/studio-take-to-score.mid";
    QFile out(tmpPath);
    if (!out.open(QIODevice::WriteOnly) || out.write(file) != file.size()) {
        return fail("Could not write " + tmpPath);
    }
    out.close();

    // import it (quantized by Score's MIDI import)
    project::INotationProjectPtr tmp = projectCreator()->newProject(iocContext());
    if (!tmp || !tmp->load(muse::io::path_t(tmpPath))) {
        return fail("Score could not read the take");
    }
    auto tmpMaster = tmp->masterNotation();
    MasterScore* ts = tmpMaster ? tmpMaster->masterScore() : nullptr;
    if (!ts || !ts->firstMeasure() || ts->nstaves() == 0) {
        return fail("Score could not read the take");
    }
    auto tmpInteraction = tmpMaster->notation()->interaction();
    tmpInteraction->select({ ts->firstMeasure() }, SelectType::RANGE, 0);
    tmpInteraction->select({ ts->lastMeasure() }, SelectType::RANGE, std::min<staff_idx_t>(ts->nstaves(), twoStaves ? 2 : 1) - 1);
    QMimeData* mime = tmpInteraction->selection()->qMimeData();
    if (!mime) {
        return fail("Nothing to paste");
    }

    // paste into the instrument, from the bar where the take starts; the clipboard is put back after
    QClipboard* cb = QGuiApplication::clipboard();
    QMimeData* saved = new QMimeData();
    if (const QMimeData* cur = cb->mimeData()) {
        for (const QString& fmt : cur->formats()) {
            saved->setData(fmt, cur->data(fmt));
        }
    }
    cb->setMimeData(mime);
    auto interaction = master->notation()->interaction();
    interaction->select({ startMeasure }, SelectType::RANGE, staffIdx);
    interaction->pasteSelection();
    cb->setMimeData(saved);
    return true;
}
