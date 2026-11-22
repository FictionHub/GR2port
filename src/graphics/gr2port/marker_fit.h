// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>

#include "common/types.h"

namespace Xbyak {
class CodeGenerator;
}

// The game places a marker, such as an aiming target's ring or a mission's waypoint, at the point
// of its 1920 x 1080 canvas where the marked object appears. On a screen of another shape the
// screen drawing keeps its 16:9 shape in the middle of the screen, which would put the marker
// nearer the middle than the object. A marker's offset in the canvas therefore grows by the inverse
// of the fit scale, so the fit brings it back onto the object at its own 16:9 shape. The game's
// tests of whether a marker is on the screen read the position before it grows, and its test of a
// layout element's bounds against the canvas passes a marker's.
namespace Gr2Port::MarkerFit {

/// What a marker's draws hold in the last column of their matrix's third row, a row the programs
/// that place the screen drawing do not read.
inline constexpr float DrawTag = -7654321.0f;

/// Hooks the calls that place the markers' layouts and name plates. Returns whether all went in.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

/// Sets the scale the screen drawing fits the screen by. Without a successful Install, does
/// nothing.
void SetFitScale(std::array<float, 2> fit_scale);

} // namespace Gr2Port::MarkerFit
