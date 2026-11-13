/*
 * Scenes (Laurence Who Studio)
 * AudioEngine: every layer of the current scene playing through one PortAudio stream,
 * as close to the keys as the sound card allows (WASAPI exclusive or ASIO, 64-128 frames).
 *
 * Threads: the MIDI callback (any thread) pushes notes into a lock-free queue that the
 * audio callback reads; the main thread builds a "program" (the scene's layers with their
 * loaded engines) and hands it over with one atomic swap. Notes held across a scene change
 * keep sounding on the sound that started them and get their note-off there.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <QObject>
#include <QString>

#include "scenesformat.h"

namespace au::instruments {
class Engine;
}

namespace scenes {
struct AudioDevice {
    int index = -1;         // PortAudio device index
    QString name;           // "Speakers (Realtek)"
    QString hostApi;        // "Windows WASAPI", "ASIO", ...
    double defaultRate = 48000.0;
    bool isWasapi = false;
    bool isAsio = false;
};

struct AudioSettings {
    QString deviceName;     // "" = the best default (WASAPI default output)
    QString hostApi;
    int bufferFrames = 128;
    bool exclusive = true;  // WASAPI exclusive mode: the shortest path to the speakers
    double sampleRate = 0;  // 0 = the device's own rate
};

class AudioEngine : public QObject
{
    Q_OBJECT
public:
    explicit AudioEngine(QObject* parent = nullptr);
    ~AudioEngine() override;

    // ---- device ----
    std::vector<AudioDevice> devices();
    bool start(const AudioSettings& s);
    void stop();
    bool isRunning() const { return m_stream != nullptr; }
    QString status() const;          // "Speakers · WASAPI exclusive · 48 kHz · 128 frames · 4.0 ms"
    QString error() const { return m_error; }
    double outputLatencyMs() const { return m_latencyMs; }
    double bufferMs() const { return m_rate > 0 ? 1000.0 * m_frames / m_rate : 0.0; }
    double cpuLoad() const;
    double sampleRate() const { return m_rate; }
    const AudioSettings& settings() const { return m_settings; }

    // ---- sounds ----
    //! loads (in the background) every sound the set uses; the current scene first
    void preload(const SceneSet& set, int current);
    //! the scene that plays from now on (switches as soon as its sounds are loaded)
    void setScene(const Scene& scene);
    bool isReady(const Scene& scene) const;
    int loadingCount() const;

    // ---- playing (any thread, lock-free for the audio thread) ----
    void noteOn(int note, int velocity);
    void noteOff(int note);
    void controlChange(int cc, int value);
    void pitchBend(int value); // -8192..8191
    void allNotesOff();

    void setMasterVolume(float v) { m_master = v; }
    float masterVolume() const { return m_master; }
    float takePeak() { return m_peak.exchange(0.0f); }
    //! keys held right now (for the keyboard strip)
    bool isKeyDown(int note) const { return note >= 0 && note < 128 && m_keyDown[note].load(std::memory_order_relaxed); }

    //! called by the PortAudio callback
    void render(float* out, unsigned long frames);

signals:
    void soundLoaded(const QString& id, bool ok);
    void stateChanged();

private:
    struct LayerRt {
        au::instruments::Engine* engine = nullptr;
        int lo = 0, hi = 127, tr = 0;
        float vol = 0.9f;
    };
    struct Program {
        std::vector<LayerRt> layers;
    };
    struct Event {
        uint8_t type = 0; // 1 on, 2 off, 3 cc, 4 bend, 5 all off
        uint8_t a = 0, b = 0;
        int16_t value = 0;
    };
    struct Loaded {
        au::instruments::Engine* engine = nullptr;
        bool failed = false;
    };

    // multi-producer (spin-locked push) / single consumer (lock-free pop)
    struct EventQueue {
        static constexpr size_t N = 4096;
        std::array<Event, N> items {};
        std::atomic<size_t> head { 0 };
        std::atomic<size_t> tail { 0 };
        std::atomic_flag lock = ATOMIC_FLAG_INIT;
        void push(const Event& e);
        bool pop(Event& e);
    };
    struct ProgramQueue { // audio thread -> main thread: programs to free
        static constexpr size_t N = 64;
        std::array<Program*, N> items {};
        std::atomic<size_t> head { 0 };
        std::atomic<size_t> tail { 0 };
        bool push(Program* p);
        bool pop(Program*& p);
    };

    static QString engineKey(const SoundRef& s, int slot);
    au::instruments::Engine* makeEngine(const SoundRef& s, float rate) const;
    void request(const SoundRef& s, int slot, bool front);
    void loaderLoop();
    void publish();
    void collectRetired();
    void push(const Event& e) { m_events.push(e); }

    // stream
    void* m_stream = nullptr;
    bool m_paInit = false;
    double m_rate = 48000.0;
    int m_frames = 128;
    double m_latencyMs = 0.0;
    QString m_deviceLabel;
    QString m_error;
    AudioSettings m_settings;

    // engines (owned here, by key "id|slot"); the audio thread only reads the programs
    std::map<QString, Loaded> m_engines;
    std::vector<Program*> m_live;     // published and not yet handed back
    Scene m_scene;
    bool m_hasScene = false;

    // loader
    struct Job { SoundRef sound; QString key; };
    std::thread m_loader;
    std::mutex m_jobMutex;
    std::condition_variable m_jobCv;
    std::deque<Job> m_jobs;
    std::vector<QString> m_pending;   // keys queued or loading (main thread)
    bool m_quit = false;
    std::atomic<float> m_loaderRate { 48000.0f };

    // audio thread state
    std::atomic<Program*> m_next { nullptr };
    Program* m_cur = nullptr;
    Program* m_prev = nullptr;        // the scene before: notes still held there ring on
    uint64_t m_prevAge = 0;
    EventQueue m_events;
    ProgramQueue m_retired;
    struct Held { au::instruments::Engine* engine; uint8_t note; };
    std::array<std::array<Held, 16>, 128> m_held {};
    std::array<uint8_t, 128> m_heldCount {};
    bool m_sustain = false;
    std::vector<float> m_l, m_r, m_mixL, m_mixR;
    std::array<std::atomic<bool>, 128> m_keyDown {};
    std::atomic<float> m_master { 0.9f };
    std::atomic<float> m_peak { 0.0f };
};
}
