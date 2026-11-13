/*
 * Tape (Laurence Who Studio) — mic cue
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "miccue.h"

#include <cmath>

#include <portaudio.h>

using namespace au::instruments;

namespace {
constexpr double RATE = 48000.0;

int callback(const void* input, void*, unsigned long frames, const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* user)
{
    if (input) {
        static_cast<MicCue*>(user)->process(static_cast<const float*>(input), frames);
    }
    return paContinue;
}
}

MicCue::MicCue(std::function<void()> onCue)
    : m_onCue(std::move(onCue))
{
    m_poll.setInterval(10);
    QObject::connect(&m_poll, &QTimer::timeout, [this]() {
        if (m_heard.exchange(false)) {
            stop();
            if (m_onCue) {
                m_onCue();
            }
        }
    });
}

MicCue::~MicCue()
{
    stop();
}

void MicCue::start()
{
    if (m_stream) {
        return;
    }
    if (Pa_Initialize() != paNoError) {
        return;
    }
    m_heard = false;
    m_floor = 0.0;
    m_armFrames = long(RATE * 0.15); // ignore the first 150 ms (the room settles, the pause's last sound)
    PaStream* s = nullptr;
    if (Pa_OpenDefaultStream(&s, 1, 0, paFloat32, RATE, 256, callback, this) != paNoError || !s) {
        Pa_Terminate();
        return;
    }
    if (Pa_StartStream(s) != paNoError) {
        Pa_CloseStream(s);
        Pa_Terminate();
        return;
    }
    m_stream = s;
    m_poll.start();
}

void MicCue::stop()
{
    m_poll.stop();
    if (!m_stream) {
        return;
    }
    PaStream* s = static_cast<PaStream*>(m_stream);
    m_stream = nullptr;
    Pa_StopStream(s);
    Pa_CloseStream(s);
    Pa_Terminate();
}

void MicCue::process(const float* in, unsigned long frames)
{
    double sum = 0.0;
    for (unsigned long i = 0; i < frames; ++i) {
        sum += double(in[i]) * in[i];
    }
    const double rms = std::sqrt(sum / std::max<unsigned long>(1, frames));
    if (m_armFrames > 0) {
        m_armFrames -= long(frames);
        m_floor = m_floor == 0.0 ? rms : m_floor * 0.8 + rms * 0.2;
        return;
    }
    // a note: well above the room's level, and loud enough to be playing
    if (rms > std::max(0.015, m_floor * 5.0)) {
        m_heard = true;
        return;
    }
    m_floor = m_floor * 0.98 + rms * 0.02;
}
