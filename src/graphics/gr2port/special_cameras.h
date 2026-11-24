// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"

namespace Xbyak {
class CodeGenerator;
}

// Two cameras turn by the right stick without the game's Camera Sensitivity: the flight of the
// Normal style's Special Attack, and the camera of Panther Mode and the Gravity Slide. Each turns
// by a step of the stick once a frame, made for 30 frames per second; above 30 the stubs shrink
// the step to the frame's share of a 30 fps frame, and the Panther Mode camera's step to what a
// 30 fps frame keeps of it once that camera moves back toward where it was. The Special Attack's
// flight and Panther Mode then turn at their own sensitivity, which the camera page of the System
// Menu sets.
namespace Gr2Port::SpecialCameras {

/// Hooks both cameras' turns, with the sensitivities of the settings.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

/// Any thread: the turn speeds of the Special Attack's flight and of Panther Mode, in percent of
/// the game's own, from the next frame.
void Set(u32 special_attack, u32 panther);

} // namespace Gr2Port::SpecialCameras
