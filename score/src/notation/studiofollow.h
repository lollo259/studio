/*
 * Studio: "Follow me". When on, playback follows you on the MIDI keyboard: it waits at each of
 * your notes until you play it, and jumps ahead when you play early.
 */
#pragma once

namespace mu::notation::studio {
inline bool& followMeOn()
{
    static bool on = false;
    return on;
}
}
