/*
 * Scenes (Laurence Who Studio)
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "midiin.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

using namespace scenes;

#ifdef _WIN32
static void CALLBACK midiInProc(HMIDIIN, UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR)
{
    if (msg == MIM_DATA) {
        reinterpret_cast<MidiIn*>(instance)->dispatch(static_cast<unsigned long>(param1));
    }
}
#endif

MidiIn::MidiIn(Handler handler, QObject* parent)
    : QObject(parent), m_handler(std::move(handler))
{
    // keyboards come and go on stage: look every 2 s and re-open when the list changes
    m_poll.setInterval(2000);
    connect(&m_poll, &QTimer::timeout, this, [this]() {
        if (available() != m_known) {
            rescan();
        }
    });
    m_poll.start();
    rescan();
}

MidiIn::~MidiIn()
{
    closeAll();
}

void MidiIn::dispatch(unsigned long msg)
{
    const unsigned char status = msg & 0xFF;
    if (status < 0x80 || status >= 0xF0) {
        return; // running status is resolved by the driver; ignore clock/sysex/active sensing
    }
    if (m_handler) {
        m_handler(status, (msg >> 8) & 0x7F, (msg >> 16) & 0x7F);
    }
}

QStringList MidiIn::available()
{
    QStringList names;
#ifdef _WIN32
    const UINT n = midiInGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        MIDIINCAPSW caps {};
        if (midiInGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            names << QString::fromWCharArray(caps.szPname);
        } else {
            names << QString();
        }
    }
#endif
    return names;
}

void MidiIn::closeAll()
{
#ifdef _WIN32
    for (void* h : m_handles) {
        midiInStop(static_cast<HMIDIIN>(h));
        midiInReset(static_cast<HMIDIIN>(h));
        midiInClose(static_cast<HMIDIIN>(h));
    }
#endif
    m_handles.clear();
    m_open.clear();
}

void MidiIn::rescan()
{
    closeAll();
    m_known = available();
#ifdef _WIN32
    for (UINT i = 0; i < UINT(m_known.size()); ++i) {
        HMIDIIN h = nullptr;
        if (midiInOpen(&h, i, reinterpret_cast<DWORD_PTR>(&midiInProc), reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
            continue; // in use by another app
        }
        midiInStart(h);
        m_handles.push_back(h);
        m_open << m_known.at(int(i));
    }
#endif
    emit devicesChanged();
}
