/*
 * Scenes (Laurence Who Studio)
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "audioengine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <QDir>
#include <QMetaObject>

#include <portaudio.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#if __has_include(<pa_win_wasapi.h>)
#include <pa_win_wasapi.h>
#define SCENES_HAS_WASAPI_API 1
#endif
#endif

#include "../instruments/internal/engines.h"

using namespace scenes;
using au::instruments::Engine;

namespace {
constexpr int MAX_BLOCK = 1024;

int paCallback(const void*, void* output, unsigned long frames, const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* user)
{
    static_cast<AudioEngine*>(user)->render(static_cast<float*>(output), frames);
    return paContinue;
}

std::string nativePath(const QString& p)
{
    // sfizz and FluidSynth open files with narrow (ANSI) paths, like Tape does
    return QDir::toNativeSeparators(p).toLocal8Bit().toStdString();
}

inline float softClip(float x)
{
    // transparent below 0.8, then rounds off smoothly towards 1.0 instead of clipping hard
    const float a = std::fabs(x);
    if (a <= 0.8f) {
        return x;
    }
    const float over = (a - 0.8f) / 0.2f;
    const float y = 0.8f + 0.2f * (over / (1.0f + over));
    return x < 0 ? -y : y;
}
}

// ---------------------------------------------------------------- queues

void AudioEngine::EventQueue::push(const Event& e)
{
    while (lock.test_and_set(std::memory_order_acquire)) {
        // producers only (MIDI threads, main thread); the audio thread never takes this lock
    }
    const size_t h = head.load(std::memory_order_relaxed);
    const size_t next = (h + 1) % N;
    if (next != tail.load(std::memory_order_acquire)) {
        items[h] = e;
        head.store(next, std::memory_order_release);
    }
    lock.clear(std::memory_order_release);
}

bool AudioEngine::EventQueue::pop(Event& e)
{
    const size_t t = tail.load(std::memory_order_relaxed);
    if (t == head.load(std::memory_order_acquire)) {
        return false;
    }
    e = items[t];
    tail.store((t + 1) % N, std::memory_order_release);
    return true;
}

bool AudioEngine::ProgramQueue::push(Program* p)
{
    const size_t h = head.load(std::memory_order_relaxed);
    const size_t next = (h + 1) % N;
    if (next == tail.load(std::memory_order_acquire)) {
        return false;
    }
    items[h] = p;
    head.store(next, std::memory_order_release);
    return true;
}

bool AudioEngine::ProgramQueue::pop(Program*& p)
{
    const size_t t = tail.load(std::memory_order_relaxed);
    if (t == head.load(std::memory_order_acquire)) {
        return false;
    }
    p = items[t];
    tail.store((t + 1) % N, std::memory_order_release);
    return true;
}

// ---------------------------------------------------------------- life

AudioEngine::AudioEngine(QObject* parent)
    : QObject(parent)
{
    m_l.resize(MAX_BLOCK);
    m_r.resize(MAX_BLOCK);
    m_mixL.resize(MAX_BLOCK);
    m_mixR.resize(MAX_BLOCK);
    if (Pa_Initialize() == paNoError) {
        m_paInit = true;
    } else {
        m_error = "audio: PortAudio did not start";
    }
    m_loader = std::thread([this] { loaderLoop(); });
}

AudioEngine::~AudioEngine()
{
    stop();
    {
        std::lock_guard<std::mutex> lock(m_jobMutex);
        m_quit = true;
        m_jobs.clear();
    }
    m_jobCv.notify_all();
    if (m_loader.joinable()) {
        m_loader.join();
    }
    m_next.store(nullptr); // every published program is in m_live
    for (Program* p : m_live) {
        delete p;
    }
    m_live.clear();
    m_cur = m_prev = nullptr;
    for (auto& [k, l] : m_engines) {
        delete l.engine;
    }
    m_engines.clear();
    if (m_paInit) {
        Pa_Terminate();
    }
}

// ---------------------------------------------------------------- device

std::vector<AudioDevice> AudioEngine::devices()
{
    std::vector<AudioDevice> out;
    if (!m_paInit) {
        return out;
    }
    const int n = Pa_GetDeviceCount();
    for (int i = 0; i < n; ++i) {
        const PaDeviceInfo* d = Pa_GetDeviceInfo(i);
        if (!d || d->maxOutputChannels < 2) {
            continue;
        }
        const PaHostApiInfo* api = Pa_GetHostApiInfo(d->hostApi);
        AudioDevice a;
        a.index = i;
        a.name = QString::fromUtf8(d->name ? d->name : "");
        a.hostApi = QString::fromUtf8(api && api->name ? api->name : "");
        a.defaultRate = d->defaultSampleRate;
        a.isWasapi = api && api->type == paWASAPI;
        a.isAsio = api && api->type == paASIO;
        out.push_back(a);
    }
    // lowest latency first: ASIO, WASAPI, then the rest (MME/DirectSound are slow)
    std::stable_sort(out.begin(), out.end(), [](const AudioDevice& a, const AudioDevice& b) {
        auto rank = [](const AudioDevice& d) { return d.isAsio ? 0 : d.isWasapi ? 1 : 2; };
        return rank(a) < rank(b);
    });
    return out;
}

bool AudioEngine::start(const AudioSettings& s)
{
    stop();
    m_settings = s;
    if (!m_paInit) {
        if (Pa_Initialize() != paNoError) {
            m_error = "audio: PortAudio did not start";
            emit stateChanged();
            return false;
        }
        m_paInit = true;
    }

    // the device: the one asked for, else the WASAPI default output, else any default
    PaDeviceIndex device = paNoDevice;
    for (const AudioDevice& d : devices()) {
        if (!s.deviceName.isEmpty() && d.name == s.deviceName && (s.hostApi.isEmpty() || d.hostApi == s.hostApi)) {
            device = d.index;
            break;
        }
    }
#ifdef _WIN32
    if (device == paNoDevice) {
        const PaHostApiIndex wasapi = Pa_HostApiTypeIdToHostApiIndex(paWASAPI);
        if (wasapi >= 0) {
            const PaHostApiInfo* api = Pa_GetHostApiInfo(wasapi);
            if (api && api->defaultOutputDevice != paNoDevice) {
                device = api->defaultOutputDevice;
            }
        }
    }
#endif
    if (device == paNoDevice) {
        device = Pa_GetDefaultOutputDevice();
    }
    if (device == paNoDevice) {
        m_error = "no audio output found";
        emit stateChanged();
        return false;
    }

    const PaDeviceInfo* info = Pa_GetDeviceInfo(device);
    const PaHostApiInfo* api = Pa_GetHostApiInfo(info->hostApi);
    const bool isWasapi = api && api->type == paWASAPI;
    const double rate = s.sampleRate > 0 ? s.sampleRate : (info->defaultSampleRate > 0 ? info->defaultSampleRate : 48000.0);
    const int frames = std::clamp(s.bufferFrames, 16, MAX_BLOCK);

    PaStreamParameters out {};
    out.device = device;
    out.channelCount = 2;
    out.sampleFormat = paFloat32;
    // ask for exactly one buffer of latency: the host adds what it really needs
    out.suggestedLatency = double(frames) / rate;
    out.hostApiSpecificStreamInfo = nullptr;

    bool exclusive = false;
    PaStream* stream = nullptr;
    PaError err = paNoError;
#ifdef SCENES_HAS_WASAPI_API
    PaWasapiStreamInfo wasapiInfo {};
    if (isWasapi) {
        wasapiInfo.size = sizeof(PaWasapiStreamInfo);
        wasapiInfo.hostApiType = paWASAPI;
        wasapiInfo.version = 1;
        wasapiInfo.threadPriority = eThreadPriorityProAudio;
        if (s.exclusive) {
            // exclusive: Scenes talks to the sound card directly, no Windows mixer in between
            wasapiInfo.flags = paWinWasapiExclusive | paWinWasapiThreadPriority;
            out.hostApiSpecificStreamInfo = &wasapiInfo;
            err = Pa_OpenStream(&stream, nullptr, &out, rate, frames, paClipOff | paDitherOff, &paCallback, this);
            exclusive = err == paNoError;
        }
        if (!exclusive) {
            // shared (another app has the card, or it refuses the format): Windows mixes, a little later
            stream = nullptr;
            wasapiInfo.flags = paWinWasapiThreadPriority | paWinWasapiAutoConvert;
            out.hostApiSpecificStreamInfo = &wasapiInfo;
            err = Pa_OpenStream(&stream, nullptr, &out, rate, frames, paClipOff | paDitherOff, &paCallback, this);
        }
    } else
#endif
    {
        err = Pa_OpenStream(&stream, nullptr, &out, rate, frames, paClipOff | paDitherOff, &paCallback, this);
    }
    if (err != paNoError && frames != paFramesPerBufferUnspecified) {
        // some drivers only take their own buffer size
        out.hostApiSpecificStreamInfo = nullptr;
        out.suggestedLatency = info->defaultLowOutputLatency;
        stream = nullptr;
        err = Pa_OpenStream(&stream, nullptr, &out, rate, paFramesPerBufferUnspecified, paClipOff | paDitherOff, &paCallback, this);
    }
    if (err != paNoError) {
        m_error = QString("audio: ") + Pa_GetErrorText(err);
        emit stateChanged();
        return false;
    }

    // the stream is not running yet: the engines can change rate safely
    if (rate != m_rate) {
        m_rate = rate;
        m_loaderRate = float(rate);
        for (auto& [k, l] : m_engines) {
            if (l.engine) {
                l.engine->setSampleRate(float(rate));
            }
        }
    }
    m_frames = frames;

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        Pa_CloseStream(stream);
        m_error = QString("audio: ") + Pa_GetErrorText(err);
        emit stateChanged();
        return false;
    }
    m_stream = stream;
    const PaStreamInfo* si = Pa_GetStreamInfo(stream);
    m_latencyMs = si ? si->outputLatency * 1000.0 : 0.0;
    QString apiName = api && api->name ? QString::fromUtf8(api->name) : QString();
    apiName.remove("Windows ");
    m_deviceLabel = QString::fromUtf8(info->name ? info->name : "") + " · " + apiName.toLower()
                    + (isWasapi ? (exclusive ? " exclusive" : " shared") : "");
    m_error.clear();
    emit stateChanged();
    return true;
}

void AudioEngine::stop()
{
    if (!m_stream) {
        return;
    }
    Pa_StopStream(static_cast<PaStream*>(m_stream));
    Pa_CloseStream(static_cast<PaStream*>(m_stream));
    m_stream = nullptr;
    emit stateChanged();
}

QString AudioEngine::status() const
{
    if (!m_error.isEmpty()) {
        return m_error;
    }
    if (!m_stream) {
        return "sound off";
    }
    return QString("%1 · %2 kHz · %3 frames · %4 ms")
           .arg(m_deviceLabel)
           .arg(m_rate / 1000.0, 0, 'f', m_rate == 44100.0 ? 1 : 0)
           .arg(m_frames)
           .arg(m_latencyMs, 0, 'f', 1);
}

double AudioEngine::cpuLoad() const
{
    return m_stream ? Pa_GetStreamCpuLoad(static_cast<PaStream*>(m_stream)) : 0.0;
}

// ---------------------------------------------------------------- sounds

QString AudioEngine::engineKey(const SoundRef& s, int slot)
{
    return s.id + "|" + QString::number(slot);
}

Engine* AudioEngine::makeEngine(const SoundRef& s, float rate) const
{
    if (s.isBuiltin()) {
        for (const Builtin& b : builtins()) {
            if (s.id == QLatin1String(b.id)) {
                return au::instruments::makeSfzEngineFromText(rate, MAX_BLOCK, std::string("/") + (b.id + 2) + ".sfz", b.sfz);
            }
        }
        return nullptr;
    }
    if (s.path.isEmpty()) {
        return nullptr;
    }
    if (s.soundFont) {
        return au::instruments::makeSoundFontEngine(rate, nativePath(s.path), s.bank, s.program);
    }
    return au::instruments::makeSfzEngine(rate, MAX_BLOCK, nativePath(s.path));
}

void AudioEngine::request(const SoundRef& s, int slot, bool front)
{
    const QString key = engineKey(s, slot);
    if (m_engines.count(key) || std::find(m_pending.begin(), m_pending.end(), key) != m_pending.end()) {
        return;
    }
    m_pending.push_back(key);
    std::lock_guard<std::mutex> lock(m_jobMutex);
    if (front) {
        m_jobs.push_front({ s, key });
    } else {
        m_jobs.push_back({ s, key });
    }
    m_jobCv.notify_one();
}

void AudioEngine::loaderLoop()
{
    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(m_jobMutex);
            m_jobCv.wait(lock, [this] { return m_quit || !m_jobs.empty(); });
            if (m_quit) {
                return;
            }
            job = m_jobs.front();
            m_jobs.pop_front();
        }
        const float rate = m_loaderRate.load();
        Engine* e = makeEngine(job.sound, rate);
        QMetaObject::invokeMethod(this, [this, job, e, rate]() {
            m_pending.erase(std::remove(m_pending.begin(), m_pending.end(), job.key), m_pending.end());
            if (e && rate != float(m_rate)) {
                e->setSampleRate(float(m_rate)); // the device changed while it loaded; not playing yet
            }
            Loaded& l = m_engines[job.key];
            delete l.engine; // (never set twice, but be safe)
            l.engine = e;
            l.failed = e == nullptr;
            emit soundLoaded(job.sound.id, e != nullptr);
            if (m_hasScene) {
                publish();
            }
            emit stateChanged();
        }, Qt::QueuedConnection);
    }
}

static std::vector<std::pair<SoundRef, int> > slotsOf(const Scene& sc)
{
    std::vector<std::pair<SoundRef, int> > out;
    std::map<QString, int> seen;
    for (const Layer& l : sc.layers) {
        out.push_back({ l.sound, seen[l.sound.id]++ });
    }
    return out;
}

void AudioEngine::preload(const SceneSet& set, int current)
{
    collectRetired();
    // load order: this scene, the next, the one before, then outwards
    std::vector<int> order;
    const int n = int(set.scenes.size());
    for (int d = 0; d < n; ++d) {
        if (current + d < n) {
            order.push_back(current + d);
        }
        if (d > 0 && current - d >= 0) {
            order.push_back(current - d);
        }
    }
    std::vector<QString> wanted;
    bool first = true;
    for (int i : order) {
        for (const auto& [s, slot] : slotsOf(set.scenes[size_t(i)])) {
            if (s.id.isEmpty()) {
                continue;
            }
            wanted.push_back(engineKey(s, slot));
            request(s, slot, false);
        }
        if (first) {
            // the current scene jumps the queue
            std::lock_guard<std::mutex> lock(m_jobMutex);
            std::stable_partition(m_jobs.begin(), m_jobs.end(), [&](const Job& j) {
                for (const Layer& l : set.scenes[size_t(i)].layers) {
                    if (l.sound.id == j.sound.id) {
                        return true;
                    }
                }
                return false;
            });
            first = false;
        }
    }
    // free what this set no longer uses (and no playing scene holds)
    for (auto it = m_engines.begin(); it != m_engines.end();) {
        bool used = std::find(wanted.begin(), wanted.end(), it->first) != wanted.end();
        for (Program* p : m_live) {
            for (const LayerRt& l : p->layers) {
                used = used || (l.engine && l.engine == it->second.engine);
            }
        }
        if (!used) {
            delete it->second.engine;
            it = m_engines.erase(it);
        } else {
            ++it;
        }
    }
}

bool AudioEngine::isReady(const Scene& scene) const
{
    for (const auto& [s, slot] : slotsOf(scene)) {
        auto it = m_engines.find(engineKey(s, slot));
        if (it == m_engines.end()) {
            return false;
        }
    }
    return true;
}

int AudioEngine::loadingCount() const
{
    return int(m_pending.size());
}

void AudioEngine::setScene(const Scene& scene)
{
    m_scene = scene;
    m_hasScene = true;
    for (const auto& [s, slot] : slotsOf(scene)) {
        request(s, slot, true);
    }
    publish();
}

void AudioEngine::publish()
{
    collectRetired();
    auto* p = new Program();
    const auto slotList = slotsOf(m_scene);
    for (size_t i = 0; i < m_scene.layers.size(); ++i) {
        const Layer& l = m_scene.layers[i];
        auto it = m_engines.find(engineKey(slotList[i].first, slotList[i].second));
        if (it == m_engines.end() || !it->second.engine) {
            continue;
        }
        LayerRt rt;
        rt.engine = it->second.engine;
        rt.lo = l.lo;
        rt.hi = l.hi;
        rt.tr = l.tr;
        rt.vol = float(l.vol);
        p->layers.push_back(rt);
    }
    m_live.push_back(p);
    if (Program* old = m_next.exchange(p)) {
        // never reached the audio thread: free it now
        m_live.erase(std::remove(m_live.begin(), m_live.end(), old), m_live.end());
        delete old;
    }
}

void AudioEngine::collectRetired()
{
    Program* p = nullptr;
    while (m_retired.pop(p)) {
        m_live.erase(std::remove(m_live.begin(), m_live.end(), p), m_live.end());
        delete p;
    }
}

// ---------------------------------------------------------------- playing

void AudioEngine::noteOn(int note, int velocity)
{
    if (note < 0 || note > 127) {
        return;
    }
    if (velocity <= 0) {
        noteOff(note);
        return;
    }
    m_keyDown[note].store(true, std::memory_order_relaxed);
    push({ 1, uint8_t(note), uint8_t(std::min(velocity, 127)), 0 });
}

void AudioEngine::noteOff(int note)
{
    if (note < 0 || note > 127) {
        return;
    }
    m_keyDown[note].store(false, std::memory_order_relaxed);
    push({ 2, uint8_t(note), 0, 0 });
}

void AudioEngine::controlChange(int cc, int value)
{
    push({ 3, uint8_t(cc & 0x7F), uint8_t(std::clamp(value, 0, 127)), 0 });
}

void AudioEngine::pitchBend(int value)
{
    push({ 4, 0, 0, int16_t(std::clamp(value, -8192, 8191)) });
}

void AudioEngine::allNotesOff()
{
    for (auto& k : m_keyDown) {
        k.store(false, std::memory_order_relaxed);
    }
    push({ 5, 0, 0, 0 });
}

void AudioEngine::render(float* out, unsigned long frames)
{
    auto inProgram = [](const Program* p, const Engine* e) {
        if (!p) {
            return false;
        }
        for (const LayerRt& l : p->layers) {
            if (l.engine == e) {
                return true;
            }
        }
        return false;
    };
    auto silenceAndForget = [&](Program* gone) {
        // its sounds that nobody plays any more: stop them, forget their held notes
        for (const LayerRt& l : gone->layers) {
            if (!l.engine || inProgram(m_cur, l.engine) || inProgram(m_prev, l.engine)) {
                continue;
            }
            l.engine->allSoundOff();
            for (int n = 0; n < 128; ++n) {
                uint8_t& c = m_heldCount[size_t(n)];
                for (int i = 0; i < c;) {
                    if (m_held[size_t(n)][size_t(i)].engine == l.engine) {
                        m_held[size_t(n)][size_t(i)] = m_held[size_t(n)][size_t(c - 1)];
                        --c;
                    } else {
                        ++i;
                    }
                }
            }
        }
        if (!m_retired.push(gone)) {
            // the main thread is far behind: keep it in the live list (freed at exit)
        }
    };

    // a new scene
    if (Program* n = m_next.exchange(nullptr)) {
        Program* olderPrev = m_prev;
        m_prev = m_cur;
        m_cur = n;
        m_prevAge = 0;
        if (olderPrev) {
            silenceAndForget(olderPrev);
        }
        if (m_prev) {
            // the new sounds get the pedal as it is now
            for (const LayerRt& l : m_cur->layers) {
                if (l.engine && !inProgram(m_prev, l.engine)) {
                    l.engine->cc(64, m_sustain ? 127 : 0);
                }
            }
        }
    }

    // render set: the current scene's sounds + the previous scene's that still ring
    Engine* engines[64];
    float gains[64];
    int count = 0;
    auto add = [&](Engine* e, float g) {
        if (!e || count >= 64) {
            return;
        }
        for (int i = 0; i < count; ++i) {
            if (engines[i] == e) {
                return;
            }
        }
        engines[count] = e;
        gains[count] = g;
        ++count;
    };
    if (m_cur) {
        for (const LayerRt& l : m_cur->layers) {
            add(l.engine, l.vol);
        }
    }
    if (m_prev) {
        for (const LayerRt& l : m_prev->layers) {
            add(l.engine, l.vol);
        }
    }

    Event e;
    while (m_events.pop(e)) {
        switch (e.type) {
        case 1: {
            auto& held = m_held[e.a];
            uint8_t& c = m_heldCount[e.a];
            for (int i = 0; i < c; ++i) {
                held[size_t(i)].engine->noteOff(held[size_t(i)].note);
            }
            c = 0;
            if (!m_cur) {
                break;
            }
            for (const LayerRt& l : m_cur->layers) {
                if (!l.engine || e.a < l.lo || e.a > l.hi || c >= 16) {
                    continue;
                }
                const int t = int(e.a) + l.tr;
                if (t < 0 || t > 127) {
                    continue;
                }
                l.engine->noteOn(t, e.b);
                held[c++] = { l.engine, uint8_t(t) };
            }
            break;
        }
        case 2: {
            auto& held = m_held[e.a];
            uint8_t& c = m_heldCount[e.a];
            for (int i = 0; i < c; ++i) {
                held[size_t(i)].engine->noteOff(held[size_t(i)].note);
            }
            c = 0;
            break;
        }
        case 3:
            if (e.a == 64) {
                m_sustain = e.b >= 64;
            }
            for (int i = 0; i < count; ++i) {
                engines[i]->cc(e.a, e.b);
            }
            if (e.a == 123 || e.a == 120) {
                for (int i = 0; i < count; ++i) {
                    engines[i]->allSoundOff();
                }
                m_heldCount.fill(0);
            }
            break;
        case 4:
            for (int i = 0; i < count; ++i) {
                engines[i]->pitchWheel(e.value);
            }
            break;
        case 5:
            for (int i = 0; i < count; ++i) {
                engines[i]->allSoundOff();
                engines[i]->cc(64, 0);
            }
            m_sustain = false;
            m_heldCount.fill(0);
            break;
        default:
            break;
        }
    }

    // mix in blocks of at most MAX_BLOCK
    const float master = m_master.load(std::memory_order_relaxed);
    float peak = 0.0f;
    unsigned long done = 0;
    while (done < frames) {
        const int n = int(std::min<unsigned long>(frames - done, MAX_BLOCK));
        std::fill_n(m_mixL.data(), n, 0.0f);
        std::fill_n(m_mixR.data(), n, 0.0f);
        for (int i = 0; i < count; ++i) {
            engines[i]->render(m_l.data(), m_r.data(), n);
            const float g = gains[i];
            for (int k = 0; k < n; ++k) {
                m_mixL[size_t(k)] += m_l[size_t(k)] * g;
                m_mixR[size_t(k)] += m_r[size_t(k)] * g;
            }
        }
        float* o = out + size_t(done) * 2;
        for (int k = 0; k < n; ++k) {
            const float l = softClip(m_mixL[size_t(k)] * master);
            const float r = softClip(m_mixR[size_t(k)] * master);
            o[2 * k] = l;
            o[2 * k + 1] = r;
            peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
        }
        done += unsigned(n);
    }
    if (peak > m_peak.load(std::memory_order_relaxed)) {
        m_peak.store(peak, std::memory_order_relaxed);
    }

    // the previous scene stops ringing 4 s after its last note (and the pedal) went up
    if (m_prev) {
        bool stillHeld = m_sustain;
        for (int n = 0; n < 128 && !stillHeld; ++n) {
            for (int i = 0; i < m_heldCount[size_t(n)]; ++i) {
                if (!inProgram(m_cur, m_held[size_t(n)][size_t(i)].engine)) {
                    stillHeld = true;
                    break;
                }
            }
        }
        m_prevAge = stillHeld ? 0 : m_prevAge + frames;
        if (m_prevAge > uint64_t(4.0 * m_rate)) {
            Program* gone = m_prev;
            m_prev = nullptr;
            silenceAndForget(gone);
        }
    }
}
