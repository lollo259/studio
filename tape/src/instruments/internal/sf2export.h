/*
 * Tape (Laurence Who Studio) — instruments
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <QString>

#include "samplerkit.h"

namespace au::instruments {
//! Writes the kit as a SoundFont 2 file (pads on C2-D#3, a keys-mode pad also across E3-C8).
bool writeKitSf2(const SamplerKit& kit, const QString& path, QString* error = nullptr);
}
