/*
 * Tape (Laurence Who Studio) — while Score waits at a cue, Tape listens to the microphone:
 * the first note played (a clear rise in level) sends the cue back to Score.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <atomic>
#include <functional>

#include <QTimer>

namespace au::instruments {
class MicCue
{
public:
    explicit MicCue(std::function<void()> onCue);
    ~MicCue();

    void start();
    void stop();
    bool isListening() const { return m_stream != nullptr; }

    // audio thread
    void process(const float* in, unsigned long frames);

private:
    std::function<void()> m_onCue;
    void* m_stream = nullptr;
    std::atomic<bool> m_heard { false };
    double m_floor = 0.0;
    long m_armFrames = 0;
    QTimer m_poll;
};
}
