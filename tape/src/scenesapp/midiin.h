/*
 * Scenes (Laurence Who Studio)
 * MidiIn: every MIDI keyboard on the computer at once (Windows winmm, straight from the
 * driver's callback: no extra thread hop). Re-opens them when one is plugged in or out.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <functional>
#include <vector>

#include <QObject>
#include <QStringList>
#include <QTimer>

namespace scenes {
class MidiIn : public QObject
{
    Q_OBJECT
public:
    //! called from the MIDI driver's thread: status, data1, data2
    using Handler = std::function<void (unsigned char, unsigned char, unsigned char)>;

    explicit MidiIn(Handler handler, QObject* parent = nullptr);
    ~MidiIn() override;

    QStringList openDevices() const { return m_open; }

    //! for the callback
    void dispatch(unsigned long msg);

signals:
    void devicesChanged();

private:
    void rescan();
    void closeAll();
    static QStringList available();

    Handler m_handler;
    QStringList m_known;
    QStringList m_open;
    std::vector<void*> m_handles; // HMIDIIN
    QTimer m_poll;
};
}
