/*
 * Studio: free-hand slurs. When the pen is on, a stroke drawn over the score becomes a slur
 * between the nearest notes, shaped like the stroke.
 */
#pragma once

#include <vector>

#include "draw/types/geometry.h" // muse::PointF

namespace mu::notation::studio {
inline bool& slurPenOn()
{
    static bool on = false;
    return on;
}

inline std::vector<muse::PointF>& slurPenStroke() // the stroke being drawn, canvas coordinates
{
    static std::vector<muse::PointF> stroke;
    return stroke;
}
}
