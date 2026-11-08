/*
 * Tape (Laurence Who Studio) — instruments
 * LiveSynth: sfizz (SFZ engine) playing through its own low-latency PortAudio stream.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../ilivesynth.h"

namespace sfz {
class Sfizz;
}

namespace au::instruments {
class LiveSynth : public ILiveSynth
{
public:
    LiveSynth();
    ~LiveSynth() override;

    bool start() override;
    void stop() override;
    bool isRunning() const override;
    std::string statusText() const override;
    double latencyMs() const override;

    void loadSound(const SoundEntry& sound) override;
    std::vector<SoundEntry> builtinSounds() const override;
    std::string currentSoundName() const override;
    bool isLoading() const override;

    muse::async::Notification stateChanged() const override;

    void noteOn(int note, int velocity) override;
    void noteOff(int note) override;
    void controlChange(int cc, int value) override;
    void pitchBend(int value) override;
    void allNotesOff() override;

    void setVolume(float linear) override;
    float volume() const override;

    bool startTake(const std::string& folder, const std::string& baseName) override;
    TakeResult stopTake() override;
    bool isTaking() const override;
    double takeSeconds() const override;

    //! Called by the audio stream. Public only for the C callback.
    void render(float* out, unsigned long frames);

private:
    struct Event {
        uint8_t type = 0; // 1 on, 2 off, 3 cc, 4 bend, 5 all off
        uint8_t a = 0;
        uint8_t b = 0;
        int16_t value = 0;
    };

    // single producer (main thread) / single consumer (audio thread)
    template<typename T, size_t N>
    struct SpscQueue {
        std::array<T, N> items {};
        std::atomic<size_t> head { 0 };
        std::atomic<size_t> tail { 0 };
        bool push(const T& v)
        {
            size_t h = head.load(std::memory_order_relaxed);
            size_t next = (h + 1) % N;
            if (next == tail.load(std::memory_order_acquire)) {
                return false;
            }
            items[h] = v;
            head.store(next, std::memory_order_release);
            return true;
        }

        bool pop(T& v)
        {
            size_t t = tail.load(std::memory_order_relaxed);
            if (t == head.load(std::memory_order_acquire)) {
                return false;
            }
            v = items[t];
            tail.store((t + 1) % N, std::memory_order_release);
            return true;
        }
    };

    void push(const Event& e);
    void notifyChangedOnMainThread();
    sfz::Sfizz* makeEngine() const;
    void loaderLoop();
    void writerLoop();
    void recordMidi(uint8_t status, uint8_t a, uint8_t b);

    // audio
    void* m_stream = nullptr; // PaStream*
    bool m_paInitialized = false;
    double m_sampleRate = 48000.0;
    double m_latencyMs = 0.0;
    std::string m_deviceName;
    std::string m_error;

    // engines: m_current is used only by the audio thread while the stream runs
    sfz::Sfizz* m_current = nullptr;
    std::atomic<sfz::Sfizz*> m_next { nullptr };
    std::atomic<sfz::Sfizz*> m_retired { nullptr };
    SpscQueue<Event, 2048> m_events;
    std::vector<float> m_left;
    std::vector<float> m_right;
    std::atomic<float> m_volume { 0.9f };
    std::atomic<uint64_t> m_framesRendered { 0 };

    // background loading
    std::thread m_loader;
    std::mutex m_loadMutex;
    std::condition_variable m_loadCv;
    bool m_hasPendingLoad = false;
    SoundEntry m_pendingSound;
    std::atomic<bool> m_loading { false };
    std::atomic<bool> m_quit { false };
    mutable std::mutex m_nameMutex;
    std::string m_currentName;

    // takes
    std::atomic<bool> m_taking { false };
    std::atomic<uint64_t> m_takeStartFrame { 0 };
    SpscQueue<float, 48000 * 2 * 8> m_takeAudio; // ~8 s of stereo at 48 kHz in flight
    std::thread m_writer;
    std::string m_takeAudioPath;
    std::string m_takeMidiPath;
    std::vector<std::pair<uint64_t, std::array<uint8_t, 3> > > m_takeMidi; // frame, bytes (main thread)
    uint64_t m_takeFramesWritten = 0;

    muse::async::Notification m_stateChanged;
};
}
