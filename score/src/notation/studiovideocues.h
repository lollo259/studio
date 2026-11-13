/*
 * Studio: cues placed on Tape's video ("hit points"). Tape sends their times; Score draws a thin
 * line at the exact place in the bars (View → Video cues shows/hides them) so notes can be put
 * right on them. They are not part of the score.
 */
#pragma once

#include <vector>

namespace mu::notation::studio {
inline bool& videoCuesShown()
{
    static bool on = true;
    return on;
}

struct VideoHitPoint {
    double secs = 0.0;   // on the score's timeline (Tape time)
    int number = 0;
};

inline std::vector<VideoHitPoint>& videoHitPoints()
{
    static std::vector<VideoHitPoint> points;
    return points;
}
}
