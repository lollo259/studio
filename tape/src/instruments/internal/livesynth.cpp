/*
 * Tape (Laurence Who Studio) — instruments
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "livesynth.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <QCoreApplication>
#include <QTimer>

#include <portaudio.h>
#include <sfizz.hpp>

using namespace au::instruments;

namespace {
constexpr int MAX_BLOCK = 1024;
constexpr const char* BUILTIN_PREFIX = "builtin:";

struct Builtin {
    const char* id;
    const char* name;
    SoundFamily family;
    const char* sfz;
};

const Builtin BUILTINS[] = {
    { "keys", "Tape · soft keys", SoundFamily::Keys,
      "<global> ampeg_attack=0.002 ampeg_decay=1.8 ampeg_sustain=25 ampeg_release=0.35 amp_veltrack=85\n"
      "<region> sample=*saw fil_type=lpf_2p cutoff=1800 fil_veltrack=3000 volume=-14\n"
      "<region> sample=*sine transpose=12 volume=-20\n" },
    { "pad", "Tape · warm pad", SoundFamily::Synth,
      "<global> ampeg_attack=0.45 ampeg_sustain=100 ampeg_release=1.4 amp_veltrack=40\n"
      "<region> sample=*saw fil_type=lpf_2p cutoff=1100 volume=-17 pitch=-7\n"
      "<region> sample=*saw fil_type=lpf_2p cutoff=1100 volume=-17 pitch=7\n" },
    { "sine", "Tape · pure sine", SoundFamily::Synth,
      "<global> ampeg_attack=0.005 ampeg_release=0.2 amp_veltrack=90\n"
      "<region> sample=*sine volume=-8\n" },
};

int paCallback(const void*, void* output, unsigned long frames, const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* user)
{
    static_cast<LiveSynth*>(user)->render(static_cast<float*>(output), frames);
    return paContinue;
}

void writeLE32(std::ofstream& f, uint32_t v) { f.put(char(v)); f.put(char(v >> 8)); f.put(char(v >> 16)); f.put(char(v >> 24)); }
void writeLE16(std::ofstream& f, uint16_t v) { f.put(char(v)); f.put(char(v >> 8)); }
void writeBE32(std::ofstream& f, uint32_t v) { f.put(char(v >> 24)); f.put(char(v >> 16)); f.put(char(v >> 8)); f.put(char(v)); }
void writeBE16(std::ofstream& f, uint16_t v) { f.put(char(v >> 8)); f.put(char(v)); }
void writeVarLen(std::vector<uint8_t>& out, uint32_t v)
{
    uint8_t buf[5];
    int n = 0;
    buf[n++] = v & 0x7F;
    while ((v >>= 7)) {
        buf[n++] = 0x80 | (v & 0x7F);
    }
    while (n--) {
        out.push_back(buf[n]);
    }
}

void writeWavHeader(std::ofstream& f, uint32_t sampleRate, uint32_t frames)
{
    const uint32_t dataBytes = frames * 2 * 4;
    f.seekp(0);
    f.write("RIFF", 4);
    writeLE32(f, 4 + (8 + 18) + (8 + 4) + (8 + dataBytes));
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    writeLE32(f, 18);
    writeLE16(f, 3);            // IEEE float
    writeLE16(f, 2);            // stereo
    writeLE32(f, sampleRate);
    writeLE32(f, sampleRate * 2 * 4);
    writeLE16(f, 2 * 4);
    writeLE16(f, 32);
    writeLE16(f, 0);
    f.write("fact", 4);
    writeLE32(f, 4);
    writeLE32(f, frames);
    f.write("data", 4);
    writeLE32(f, dataBytes);
}
}

LiveSynth::LiveSynth()
{
    m_left.resize(MAX_BLOCK);
    m_right.resize(MAX_BLOCK);
    m_current = makeEngine();
    const Builtin& b = BUILTINS[0];
    m_current->loadSfzString(std::string("/") + b.id + ".sfz", b.sfz);
    m_currentName = b.name;
    m_loader = std::thread([this] { loaderLoop(); });
}

LiveSynth::~LiveSynth()
{
    if (m_taking) {
        stopTake();
    }
    stop();
    m_quit = true;
    m_loadCv.notify_all();
    if (m_loader.joinable()) {
        m_loader.join();
    }
    delete m_next.exchange(nullptr);
    delete m_retired.exchange(nullptr);
    delete m_current;
    if (m_paInitialized) {
        Pa_Terminate();
    }
}

sfz::Sfizz* LiveSynth::makeEngine() const
{
    auto* s = new sfz::Sfizz();
    s->setSampleRate(static_cast<float>(m_sampleRate));
    s->setSamplesPerBlock(MAX_BLOCK);
    s->setNumVoices(96);
    return s;
}

bool LiveSynth::start()
{
    if (m_stream) {
        return true;
    }
    if (!m_paInitialized) {
        PaError err = Pa_Initialize();
        if (err != paNoError) {
            m_error = std::string("audio: ") + Pa_GetErrorText(err);
            return false;
        }
        m_paInitialized = true;
    }

    // Prefer the lowest-latency host API: WASAPI on Windows, CoreAudio on macOS, ALSA/JACK on Linux
    PaDeviceIndex device = paNoDevice;
#ifdef _WIN32
    PaHostApiIndex wasapi = Pa_HostApiTypeIdToHostApiIndex(paWASAPI);
    if (wasapi >= 0) {
        const PaHostApiInfo* api = Pa_GetHostApiInfo(wasapi);
        if (api && api->defaultOutputDevice != paNoDevice) {
            device = api->defaultOutputDevice;
        }
    }
#endif
    if (device == paNoDevice) {
        device = Pa_GetDefaultOutputDevice();
    }
    if (device == paNoDevice) {
        m_error = "no audio output found";
        return false;
    }

    const PaDeviceInfo* info = Pa_GetDeviceInfo(device);
    PaStreamParameters out {};
    out.device = device;
    out.channelCount = 2;
    out.sampleFormat = paFloat32;
    out.suggestedLatency = info->defaultLowOutputLatency;
    out.hostApiSpecificStreamInfo = nullptr;

    const double rate = info->defaultSampleRate > 0 ? info->defaultSampleRate : 48000.0;
    PaStream* stream = nullptr;
    PaError err = Pa_OpenStream(&stream, nullptr, &out, rate, paFramesPerBufferUnspecified, paClipOff, &paCallback, this);
    if (err != paNoError) {
        m_error = std::string("audio: ") + Pa_GetErrorText(err);
        return false;
    }

    if (rate != m_sampleRate) {
        m_sampleRate = rate;
        m_current->setSampleRate(static_cast<float>(rate));
    }

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        Pa_CloseStream(stream);
        m_error = std::string("audio: ") + Pa_GetErrorText(err);
        return false;
    }

    m_stream = stream;
    const PaStreamInfo* si = Pa_GetStreamInfo(stream);
    m_latencyMs = si ? si->outputLatency * 1000.0 : 0.0;
    m_deviceName = info->name ? info->name : "";
    m_error.clear();
    notifyChangedOnMainThread();
    return true;
}

void LiveSynth::stop()
{
    if (!m_stream) {
        return;
    }
    Pa_StopStream(static_cast<PaStream*>(m_stream));
    Pa_CloseStream(static_cast<PaStream*>(m_stream));
    m_stream = nullptr;
    // apply a swap that the stream never picked up
    if (sfz::Sfizz* n = m_next.exchange(nullptr)) {
        delete m_current;
        m_current = n;
    }
    notifyChangedOnMainThread();
}

bool LiveSynth::isRunning() const
{
    return m_stream != nullptr;
}

std::string LiveSynth::statusText() const
{
    {
        std::lock_guard<std::mutex> lock(m_nameMutex);
        if (!m_error.empty()) {
            return m_error;
        }
    }
    if (!m_stream) {
        return "sound off";
    }
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s · %.0f kHz · %.0f ms", m_deviceName.c_str(), m_sampleRate / 1000.0, m_latencyMs);
    return buf;
}

double LiveSynth::latencyMs() const
{
    return m_latencyMs;
}

std::vector<SoundEntry> LiveSynth::builtinSounds() const
{
    std::vector<SoundEntry> list;
    for (const Builtin& b : BUILTINS) {
        SoundEntry e;
        e.name = b.name;
        e.library = "Tape";
        e.path = std::string(BUILTIN_PREFIX) + b.id;
        e.family = b.family;
        list.push_back(e);
    }
    return list;
}

void LiveSynth::loadSound(const SoundEntry& sound)
{
    {
        std::lock_guard<std::mutex> lock(m_loadMutex);
        m_pendingSound = sound;
        m_hasPendingLoad = true;
    }
    m_loading = true;
    notifyChangedOnMainThread();
    m_loadCv.notify_one();
}

void LiveSynth::loaderLoop()
{
    while (true) {
        SoundEntry sound;
        {
            std::unique_lock<std::mutex> lock(m_loadMutex);
            m_loadCv.wait(lock, [this] { return m_hasPendingLoad || m_quit; });
            if (m_quit) {
                return;
            }
            sound = m_pendingSound;
            m_hasPendingLoad = false;
        }

        sfz::Sfizz* engine = makeEngine();
        bool ok = false;
        if (sound.path.rfind(BUILTIN_PREFIX, 0) == 0) {
            const std::string id = sound.path.substr(std::strlen(BUILTIN_PREFIX));
            for (const Builtin& b : BUILTINS) {
                if (id == b.id) {
                    ok = engine->loadSfzString(std::string("/") + b.id + ".sfz", b.sfz);
                }
            }
        } else if (!sound.isSoundFont) {
            ok = engine->loadSfzFile(sound.path);
        }

        if (!ok) {
            delete engine;
            std::lock_guard<std::mutex> lock(m_nameMutex);
            m_error = sound.isSoundFont ? "soundfonts (.sf2) arrive in the next update: pick an SFZ sound"
                                        : "could not load " + sound.name;
            m_loading = false;
            notifyChangedOnMainThread();
            continue;
        }

        if (m_stream) {
            // hand over to the audio thread, then free the old engine once it has switched
            delete m_next.exchange(engine);
            for (int i = 0; i < 200 && m_next.load() != nullptr; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            delete m_retired.exchange(nullptr);
        } else {
            delete m_current;
            m_current = engine;
        }

        {
            std::lock_guard<std::mutex> lock(m_nameMutex);
            m_currentName = sound.name;
            m_error.clear();
        }
        m_loading = false;
        notifyChangedOnMainThread();
    }
}

std::string LiveSynth::currentSoundName() const
{
    std::lock_guard<std::mutex> lock(m_nameMutex);
    return m_currentName;
}

bool LiveSynth::isLoading() const
{
    return m_loading;
}

muse::async::Notification LiveSynth::stateChanged() const
{
    return m_stateChanged;
}

void LiveSynth::notifyChangedOnMainThread()
{
    if (QCoreApplication* app = QCoreApplication::instance()) {
        QTimer::singleShot(0, app, [this]() { m_stateChanged.notify(); });
    }
}

void LiveSynth::push(const Event& e)
{
    m_events.push(e);
}

void LiveSynth::noteOn(int note, int velocity)
{
    if (velocity <= 0) {
        noteOff(note);
        return;
    }
    push({ 1, uint8_t(note), uint8_t(std::min(velocity, 127)), 0 });
    recordMidi(0x90, uint8_t(note), uint8_t(std::min(velocity, 127)));
}

void LiveSynth::noteOff(int note)
{
    push({ 2, uint8_t(note), 0, 0 });
    recordMidi(0x80, uint8_t(note), 0);
}

void LiveSynth::controlChange(int cc, int value)
{
    push({ 3, uint8_t(cc), uint8_t(std::clamp(value, 0, 127)), 0 });
    recordMidi(0xB0, uint8_t(cc), uint8_t(std::clamp(value, 0, 127)));
}

void LiveSynth::pitchBend(int value)
{
    value = std::clamp(value, -8192, 8191);
    push({ 4, 0, 0, int16_t(value) });
    const int v = value + 8192;
    recordMidi(0xE0, uint8_t(v & 0x7F), uint8_t((v >> 7) & 0x7F));
}

void LiveSynth::allNotesOff()
{
    push({ 5, 0, 0, 0 });
}

void LiveSynth::setVolume(float linear)
{
    m_volume = std::clamp(linear, 0.0f, 1.5f);
}

float LiveSynth::volume() const
{
    return m_volume;
}

void LiveSynth::render(float* out, unsigned long frames)
{
    if (sfz::Sfizz* n = m_next.exchange(nullptr)) {
        if (m_current) {
            m_current->allSoundOff();
        }
        m_retired.store(m_current);
        m_current = n;
    }

    sfz::Sfizz* s = m_current;
    Event e;
    while (m_events.pop(e)) {
        if (!s) {
            continue;
        }
        switch (e.type) {
        case 1: s->noteOn(0, e.a, e.b);
            break;
        case 2: s->noteOff(0, e.a, 0);
            break;
        case 3: s->cc(0, e.a, e.b);
            break;
        case 4: s->pitchWheel(0, e.value);
            break;
        case 5: s->allSoundOff();
            break;
        default: break;
        }
    }

    const float vol = m_volume.load();
    const bool taking = m_taking.load(std::memory_order_acquire);
    unsigned long done = 0;
    while (done < frames) {
        const unsigned long n = std::min<unsigned long>(frames - done, MAX_BLOCK);
        float* buffers[2] = { m_left.data(), m_right.data() };
        if (s) {
            s->renderBlock(buffers, n, 1);
        } else {
            std::fill_n(m_left.data(), n, 0.0f);
            std::fill_n(m_right.data(), n, 0.0f);
        }
        float* o = out + done * 2;
        for (unsigned long i = 0; i < n; ++i) {
            const float l = m_left[i] * vol;
            const float r = m_right[i] * vol;
            o[2 * i] = l;
            o[2 * i + 1] = r;
            if (taking) {
                m_takeAudio.push(l);
                m_takeAudio.push(r);
            }
        }
        done += n;
    }
    m_framesRendered.fetch_add(frames, std::memory_order_relaxed);
}

void LiveSynth::recordMidi(uint8_t status, uint8_t a, uint8_t b)
{
    if (!m_taking) {
        return;
    }
    const uint64_t frame = m_framesRendered.load() - m_takeStartFrame.load();
    m_takeMidi.push_back({ frame, { status, a, b } });
}

bool LiveSynth::startTake(const std::string& folder, const std::string& baseName)
{
    if (m_taking || !m_stream) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    m_takeAudioPath = (std::filesystem::path(folder) / (baseName + ".wav")).string();
    m_takeMidiPath = (std::filesystem::path(folder) / (baseName + ".mid")).string();
    m_takeMidi.clear();
    m_takeFramesWritten = 0;
    float dummy;
    while (m_takeAudio.pop(dummy)) {}

    m_takeStartFrame = m_framesRendered.load();
    m_taking.store(true, std::memory_order_release);
    m_writer = std::thread([this] { writerLoop(); });
    notifyChangedOnMainThread();
    return true;
}

void LiveSynth::writerLoop()
{
    std::ofstream f(m_takeAudioPath, std::ios::binary | std::ios::trunc);
    if (!f) {
        m_error = "cannot write " + m_takeAudioPath;
        return;
    }
    writeWavHeader(f, uint32_t(m_sampleRate), 0);
    std::vector<float> chunk;
    chunk.reserve(8192);
    auto drain = [&]() {
        float v;
        chunk.clear();
        while (chunk.size() < 8192 && m_takeAudio.pop(v)) {
            chunk.push_back(v);
        }
        if (!chunk.empty()) {
            f.write(reinterpret_cast<const char*>(chunk.data()), std::streamsize(chunk.size() * sizeof(float)));
            m_takeFramesWritten += chunk.size() / 2;
        }
        return !chunk.empty();
    };
    while (m_taking.load(std::memory_order_acquire)) {
        if (!drain()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    while (drain()) {}
    writeWavHeader(f, uint32_t(m_sampleRate), uint32_t(m_takeFramesWritten));
}

TakeResult LiveSynth::stopTake()
{
    TakeResult res;
    if (!m_taking) {
        return res;
    }
    m_taking.store(false, std::memory_order_release);
    if (m_writer.joinable()) {
        m_writer.join();
    }

    // MIDI file: format 0, 960 ticks per quarter, 120 bpm (so 1 s = 1920 ticks)
    std::vector<uint8_t> track;
    const uint8_t tempo[] = { 0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 };
    track.insert(track.end(), std::begin(tempo), std::end(tempo));
    uint64_t lastTick = 0;
    for (const auto& ev : m_takeMidi) {
        const uint64_t tick = uint64_t(double(ev.first) / m_sampleRate * 1920.0 + 0.5);
        writeVarLen(track, uint32_t(tick > lastTick ? tick - lastTick : 0));
        lastTick = std::max(lastTick, tick);
        track.push_back(ev.second[0]);
        track.push_back(ev.second[1]);
        track.push_back(ev.second[2]);
    }
    const uint8_t end[] = { 0x00, 0xFF, 0x2F, 0x00 };
    track.insert(track.end(), std::begin(end), std::end(end));

    std::ofstream mf(m_takeMidiPath, std::ios::binary | std::ios::trunc);
    if (mf) {
        mf.write("MThd", 4);
        writeBE32(mf, 6);
        writeBE16(mf, 0);
        writeBE16(mf, 1);
        writeBE16(mf, 960);
        mf.write("MTrk", 4);
        writeBE32(mf, uint32_t(track.size()));
        mf.write(reinterpret_cast<const char*>(track.data()), std::streamsize(track.size()));
    }

    res.audioPath = m_takeAudioPath;
    res.midiPath = m_takeMidiPath;
    res.seconds = double(m_takeFramesWritten) / m_sampleRate;
    notifyChangedOnMainThread();
    return res;
}

bool LiveSynth::isTaking() const
{
    return m_taking;
}

double LiveSynth::takeSeconds() const
{
    if (!m_taking) {
        return 0.0;
    }
    return double(m_framesRendered.load() - m_takeStartFrame.load()) / m_sampleRate;
}
