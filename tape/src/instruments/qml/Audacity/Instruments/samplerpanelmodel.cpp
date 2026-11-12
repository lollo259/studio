/*
 * Tape (Laurence Who Studio) — instruments
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "samplerpanelmodel.h"

#include <algorithm>

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantMap>

#include "instrumentsmodule.h"
#include "internal/instrumentsservice.h"

using namespace au::instruments;

namespace {
const char* KEY_KIT = "sampler/kit";
const char* KEY_DIR = "sampler/lastFolder";
const char* KEY_LIVE_SOUND = "instruments/sound"; // the sound the live synth reloads at start

std::shared_ptr<InstrumentsService> samplerService()
{
    return InstrumentsModule::service();
}

bool isAudioFile(const QString& path)
{
    static const QStringList exts = { "wav", "wave", "flac", "aif", "aiff", "ogg" };
    return exts.contains(QFileInfo(path).suffix().toLower());
}

bool isWav(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == "wav" || ext == "wave";
}

bool kitIsEmpty(const SamplerKit& kit)
{
    return std::all_of(kit.pads.begin(), kit.pads.end(), [](const SamplerPad& p) { return p.isEmpty(); });
}

QString uniqueKitName(const QString& base)
{
    const QStringList taken = kitNames();
    if (!taken.contains(base, Qt::CaseInsensitive)) {
        return base;
    }
    for (int i = 2;; ++i) {
        const QString n = QString("%1 %2").arg(base).arg(i);
        if (!taken.contains(n, Qt::CaseInsensitive)) {
            return n;
        }
    }
}
}

SamplerPanelModel::SamplerPanelModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    m_applyTimer.setSingleShot(true);
    m_applyTimer.setInterval(250);
    connect(&m_applyTimer, &QTimer::timeout, this, &SamplerPanelModel::apply);

    m_litTimer.setInterval(40);
    connect(&m_litTimer, &QTimer::timeout, this, &SamplerPanelModel::pollLit);
}

void SamplerPanelModel::load()
{
    if (auto s = samplerService()) {
        s->changed().onNotify(this, [this]() { emit statusChanged(); });
        if (!s->synth()->isRunning()) {
            s->synth()->start();
        }
    }

    const QString last = QSettings("LaurenceWho", "Tape").value(KEY_KIT).toString();
    const QStringList all = kitNames();
    bool loaded = false;
    if (!last.isEmpty()) {
        loaded = loadKit(last, m_kit);
    }
    if (!loaded && !all.isEmpty()) {
        loaded = loadKit(all.front(), m_kit);
    }
    if (!loaded) {
        m_kit = SamplerKit();
        m_kit.name = "Kit 1";
    }
    for (int i = 0; i < SAMPLER_PADS; ++i) {
        readPeaks(i);
    }
    m_selected = 0;
    emit kitChanged();
    emit padsChanged();
    emit selectionChanged();

    apply(); // the kit becomes the sound the keyboard plays
    m_litTimer.start();
}

// ── kit ─────────────────────────────────────────────────────────────

QString SamplerPanelModel::kitName() const
{
    return m_kit.name;
}

void SamplerPanelModel::setKitName(const QString& name)
{
    const QString clean = safeKitName(name);
    if (clean == m_kit.name) {
        return;
    }
    if (kitNames().contains(clean, Qt::CaseInsensitive) && clean.compare(m_kit.name, Qt::CaseInsensitive) != 0) {
        m_error = QString("There is already a kit called \"%1\".").arg(clean);
        emit statusChanged();
        emit kitChanged(); // puts the old name back in the field
        return;
    }
    const QString old = m_kit.name;
    m_kit.name = clean;
    apply();
    if (old.compare(clean, Qt::CaseInsensitive) != 0) {
        removeKitFiles(old);
    }
    if (auto s = samplerService()) {
        s->rescan();
    }
    emit kitChanged();
}

QStringList SamplerPanelModel::kits() const
{
    QStringList list = kitNames();
    if (!list.contains(m_kit.name, Qt::CaseInsensitive)) {
        list.prepend(m_kit.name);
    }
    return list;
}

void SamplerPanelModel::newKit()
{
    if (m_applyTimer.isActive()) {
        m_applyTimer.stop();
        apply();
    }
    m_kit = SamplerKit();
    m_kit.name = uniqueKitName("Kit");
    for (WavPeaks& p : m_peaks) {
        p = WavPeaks();
    }
    m_selected = 0;
    apply();
    emit kitChanged();
    emit padsChanged();
    emit selectionChanged();
}

void SamplerPanelModel::openKit(const QString& name)
{
    if (name == m_kit.name) {
        return;
    }
    if (m_applyTimer.isActive()) {
        m_applyTimer.stop();
        apply();
    }
    SamplerKit kit;
    if (!loadKit(name, kit)) {
        m_error = QString("Could not open the kit \"%1\".").arg(name);
        emit statusChanged();
        return;
    }
    m_kit = kit;
    for (int i = 0; i < SAMPLER_PADS; ++i) {
        readPeaks(i);
    }
    m_selected = 0;
    apply();
    emit kitChanged();
    emit padsChanged();
    emit selectionChanged();
}

void SamplerPanelModel::openFolder()
{
    QDir().mkpath(samplerFolder());
    QDesktopServices::openUrl(QUrl::fromLocalFile(samplerFolder()));
}

QString SamplerPanelModel::folder() const
{
    return samplerFolder();
}

void SamplerPanelModel::apply()
{
    m_applyTimer.stop();
    m_error.clear();
    const bool existed = QFile::exists(kitSfzPath(m_kit.name));
    QString error;
    if (!saveKit(m_kit, &error)) {
        m_error = error;
        emit statusChanged();
        return;
    }
    QSettings settings("LaurenceWho", "Tape");
    settings.setValue(KEY_KIT, m_kit.name);

    auto s = samplerService();
    if (!s) {
        emit statusChanged();
        return;
    }
    if (!kitIsEmpty(m_kit)) {
        SoundEntry e;
        e.name = m_kit.name.toStdString();
        e.library = "Sampler";
        e.path = kitSfzPath(m_kit.name).toStdString();
        e.family = SoundFamily::Percussion;
        s->synth()->loadSound(e);
        settings.setValue(KEY_LIVE_SOUND, QString::fromStdString(e.path));
    }
    if (existed != QFile::exists(kitSfzPath(m_kit.name))) {
        s->rescan(); // the kit appears in (or leaves) the Instruments sound list
    }
    emit statusChanged();
}

void SamplerPanelModel::changed(bool padList)
{
    if (padList) {
        emit padsChanged();
    }
    emit selectionChanged();
    m_applyTimer.start();
}

// ── pads ────────────────────────────────────────────────────────────

QVariantList SamplerPanelModel::pads() const
{
    QVariantList list;
    for (int i = 0; i < SAMPLER_PADS; ++i) {
        const SamplerPad& p = m_kit.pads[size_t(i)];
        QVariantMap m;
        m["index"] = i;
        m["name"] = p.name;
        m["loaded"] = !p.isEmpty();
        m["mode"] = QString::fromLatin1(padModeId(p.mode));
        m["choke"] = p.choke;
        m["key"] = keyName(i);
        list << m;
    }
    return list;
}

QString SamplerPanelModel::keyName(int index) const
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int n = SAMPLER_FIRST_KEY + index;
    return QString("%1%2").arg(names[n % 12]).arg(n / 12 - 1);
}

SamplerPad& SamplerPanelModel::pad()
{
    return m_kit.pads[size_t(std::clamp(m_selected, 0, SAMPLER_PADS - 1))];
}

const SamplerPad& SamplerPanelModel::pad() const
{
    return m_kit.pads[size_t(std::clamp(m_selected, 0, SAMPLER_PADS - 1))];
}

int SamplerPanelModel::selectedPad() const
{
    return m_selected;
}

void SamplerPanelModel::selectPad(int index)
{
    if (index < 0 || index >= SAMPLER_PADS || index == m_selected) {
        return;
    }
    m_selected = index;
    emit selectionChanged();
}

QString SamplerPanelModel::padName() const
{
    return pad().name;
}

void SamplerPanelModel::setPadName(const QString& name)
{
    const QString n = name.simplified().left(40);
    if (n == pad().name || pad().isEmpty()) {
        return;
    }
    pad().name = n;
    changed();
}

QString SamplerPanelModel::padFile() const
{
    return QDir::toNativeSeparators(pad().file);
}

bool SamplerPanelModel::padLoaded() const
{
    return !pad().isEmpty();
}

bool SamplerPanelModel::padCanTrim() const
{
    return !pad().isEmpty() && pad().frames > 0;
}

double SamplerPanelModel::padStart() const
{
    return pad().start;
}

void SamplerPanelModel::setPadStart(double v)
{
    v = std::clamp(v, 0.0, std::max(0.0, pad().end - 0.002));
    if (!padCanTrim() || qFuzzyCompare(v + 1.0, pad().start + 1.0)) {
        return;
    }
    pad().start = v;
    changed(false);
}

double SamplerPanelModel::padEnd() const
{
    return pad().end;
}

void SamplerPanelModel::setPadEnd(double v)
{
    v = std::clamp(v, std::min(1.0, pad().start + 0.002), 1.0);
    if (!padCanTrim() || qFuzzyCompare(v + 1.0, pad().end + 1.0)) {
        return;
    }
    pad().end = v;
    changed(false);
}

void SamplerPanelModel::resetTrim()
{
    if (!padCanTrim()) {
        return;
    }
    pad().start = 0.0;
    pad().end = 1.0;
    changed(false);
}

QString SamplerPanelModel::padMode() const
{
    return QString::fromLatin1(padModeId(pad().mode));
}

void SamplerPanelModel::setPadMode(const QString& mode)
{
    const PadMode m = padModeFromId(mode);
    if (m == pad().mode) {
        return;
    }
    if (m == PadMode::Keys) {
        // one pad at a time is spread over the keyboard; the previous one goes back to one-shot
        for (int i = 0; i < SAMPLER_PADS; ++i) {
            if (i != m_selected && m_kit.pads[size_t(i)].mode == PadMode::Keys) {
                m_kit.pads[size_t(i)].mode = PadMode::OneShot;
            }
        }
    }
    pad().mode = m;
    changed();
}

bool SamplerPanelModel::padChoke() const
{
    return pad().choke;
}

void SamplerPanelModel::setPadChoke(bool on)
{
    if (on == pad().choke) {
        return;
    }
    pad().choke = on;
    changed();
}

QString SamplerPanelModel::padInfo() const
{
    const SamplerPad& p = pad();
    if (p.isEmpty()) {
        return QString("Pad %1 (%2) is empty: drop a sound on it or press Load.").arg(m_selected + 1).arg(keyName(m_selected));
    }
    QString info = QString("Pad %1  |  %2  |  %3").arg(m_selected + 1).arg(keyName(m_selected)).arg(QFileInfo(p.file).fileName());
    const WavPeaks& w = m_peaks[size_t(m_selected)];
    if (w.ok && w.sampleRate > 0 && p.frames > 0) {
        const double secs = double(p.frames) / w.sampleRate;
        const double trimmed = secs * std::max(0.0, p.end - p.start);
        info += QString("  |  %1 s of %2 s").arg(trimmed, 0, 'f', 2).arg(secs, 0, 'f', 2);
    } else if (!QFileInfo::exists(p.file)) {
        info += "  |  file not found";
    } else {
        info += "  |  trim works on WAV files";
    }
    return info;
}

QVariantList SamplerPanelModel::peaks() const
{
    QVariantList list;
    const WavPeaks& w = m_peaks[size_t(std::clamp(m_selected, 0, SAMPLER_PADS - 1))];
    if (pad().isEmpty() || !w.ok) {
        return list;
    }
    list.reserve(int(w.peaks.size()));
    for (float v : w.peaks) {
        list << double(v);
    }
    return list;
}

void SamplerPanelModel::readPeaks(int index)
{
    SamplerPad& p = m_kit.pads[size_t(index)];
    WavPeaks& w = m_peaks[size_t(index)];
    w = WavPeaks();
    if (p.isEmpty() || !isWav(p.file)) {
        p.frames = 0;
        return;
    }
    w = readWavPeaks(p.file, 512);
    if (!w.ok) {
        p.frames = 0;
        return;
    }
    if (p.frames != w.frames) {
        // a different file under the same name: the old trim no longer means anything
        if (p.frames != 0) {
            p.start = 0.0;
            p.end = 1.0;
        }
        p.frames = w.frames;
    }
}

bool SamplerPanelModel::setPadFile(int index, const QString& path)
{
    if (index < 0 || index >= SAMPLER_PADS) {
        return false;
    }
    if (!isAudioFile(path) || !QFileInfo::exists(path)) {
        m_error = QString("Not a sound the sampler can play: %1 (use WAV, FLAC, AIFF or OGG).").arg(QFileInfo(path).fileName());
        emit statusChanged();
        return false;
    }
    SamplerPad& p = m_kit.pads[size_t(index)];
    p.file = QFileInfo(path).absoluteFilePath();
    p.name = QFileInfo(path).completeBaseName().left(40);
    p.start = 0.0;
    p.end = 1.0;
    p.frames = 0;
    readPeaks(index);
    QSettings("LaurenceWho", "Tape").setValue(KEY_DIR, QFileInfo(path).absolutePath());
    m_selected = index;
    m_error.clear();
    changed();
    return true;
}

void SamplerPanelModel::chooseSound(int index)
{
    if (index < 0 || index >= SAMPLER_PADS) {
        return;
    }
    QString dir = QSettings("LaurenceWho", "Tape").value(KEY_DIR).toString();
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    }
    const muse::io::path_t picked = interactive()->selectOpeningFileSync(
        QString("Load a sound into pad %1").arg(index + 1).toStdString(),
        muse::io::path_t(dir),
        { "Sounds (*.wav *.wave *.flac *.aif *.aiff *.ogg)", "All files (*)" });
    if (picked.empty()) {
        return;
    }
    setPadFile(index, picked.toQString());
}

bool SamplerPanelModel::dropOnPad(int index, const QVariant& urls)
{
    QList<QUrl> list;
    if (urls.canConvert<QList<QUrl> >()) {
        list = urls.value<QList<QUrl> >();
    }
    if (list.isEmpty()) {
        for (const QVariant& v : urls.toList()) {
            list << (v.canConvert<QUrl>() ? v.toUrl() : QUrl(v.toString()));
        }
    }
    if (list.isEmpty() && !urls.toString().isEmpty()) {
        list << QUrl(urls.toString());
    }
    // several files dropped at once fill this pad and the empty ones after it
    bool any = false;
    int target = index;
    for (const QUrl& u : list) {
        if (target >= SAMPLER_PADS) {
            break;
        }
        const QString path = u.isLocalFile() ? u.toLocalFile() : u.toString();
        if (!isAudioFile(path)) {
            continue;
        }
        if (setPadFile(target, path)) {
            any = true;
            do {
                ++target;
            } while (target < SAMPLER_PADS && !m_kit.pads[size_t(target)].isEmpty());
        }
    }
    if (any) {
        m_selected = index;
        emit selectionChanged();
    } else if (!list.isEmpty() && m_error.isEmpty()) {
        m_error = "Drop WAV, FLAC, AIFF or OGG files on a pad.";
        emit statusChanged();
    }
    return any;
}

void SamplerPanelModel::clearPad()
{
    if (pad().isEmpty()) {
        return;
    }
    pad() = SamplerPad();
    m_peaks[size_t(m_selected)] = WavPeaks();
    changed();
}

// ── playing ─────────────────────────────────────────────────────────

void SamplerPanelModel::previewPad(int index, bool on)
{
    if (index < 0 || index >= SAMPLER_PADS) {
        return;
    }
    auto s = samplerService();
    if (!s) {
        return;
    }
    if (on && (m_applyTimer.isActive()
               || (!s->synth()->isLoading() && QString::fromStdString(s->synth()->currentSoundName()) != m_kit.name))) {
        apply(); // play what you see (and this kit, if the keyboard was switched to another sound)
    }
    const int note = SAMPLER_FIRST_KEY + index;
    if (on) {
        selectPad(index);
        s->synth()->noteOn(note, 100);
        m_pressed |= (1 << index);
    } else {
        s->synth()->noteOff(note);
        m_pressed &= ~(1 << index);
    }
    pollLit();
}

void SamplerPanelModel::allNotesOff()
{
    if (auto s = samplerService()) {
        s->synth()->allNotesOff();
    }
    m_pressed = 0;
    pollLit();
}

void SamplerPanelModel::pollLit()
{
    int mask = m_pressed;
    if (auto s = samplerService()) {
        bool keysHeld = false;
        for (int n = SAMPLER_KEYS_LO; n <= SAMPLER_KEYS_HI && !keysHeld; ++n) {
            keysHeld = s->isNoteHeld(n);
        }
        for (int i = 0; i < SAMPLER_PADS; ++i) {
            if (s->isNoteHeld(SAMPLER_FIRST_KEY + i)
                || (keysHeld && m_kit.pads[size_t(i)].mode == PadMode::Keys && !m_kit.pads[size_t(i)].isEmpty())) {
                mask |= (1 << i);
            }
        }
    }
    if (mask != m_lit) {
        m_lit = mask;
        emit litChanged();
    }
}

int SamplerPanelModel::litMask() const
{
    return m_lit;
}

QString SamplerPanelModel::status() const
{
    if (!m_error.isEmpty()) {
        return m_error;
    }
    auto s = samplerService();
    if (!s) {
        return "instruments unavailable";
    }
    if (s->synth()->isLoading()) {
        return "loading the kit...";
    }
    if (kitIsEmpty(m_kit)) {
        return "Drop sounds on the pads to start a kit.";
    }
    const QString current = QString::fromStdString(s->synth()->currentSoundName());
    if (current != m_kit.name) {
        return QString("The keyboard is playing \"%1\"; press a pad to play this kit.").arg(current);
    }
    return QString("Pads play on keys C2-D#3 of your MIDI keyboard - ") + QString::fromStdString(s->synth()->statusText());
}
