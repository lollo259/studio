/*
 * Score (Laurence Who Studio) — InkPrint
 * Makes an exported page look printed: ink drifts, swells and thins slightly,
 * edges get a fine grain, so no two noteheads or staff lines are identical.
 * C++ port of Laurence Who's inkprint.py ("classic" style).
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <QImage>

namespace mu::iex::imagesexport {
//! gray: the rendered page (any format, white paper, black ink) at `dpi`.
//! Returns a 1-bit image of the same size with the printed look applied.
QImage inkPrint(const QImage& page, double dpi, double strength = 1.0, unsigned seed = 259);
}
