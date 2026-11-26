// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"

namespace Xbyak {
class CodeGenerator;
}

// The game starts every sound effect and voice with a pre-delay of 1/6 s, which SNDX counts down on
// its update, every 1/30 s, before the sound starts; a stop without a fade is not delayed. A sound
// reaches the system's audio output about 175 ms after the game starts it, and GR2port's picture
// reaches the swapchain two to three frames after the game logic: 62 ms at 30 fps, 20 ms at
// 140 fps. Sound came 0.1 s after the picture at 30 fps and 0.15 s at 140 fps. Above 30 fps the
// pre-delay here is shorter by the time two frames gain on two 30 fps frames, so sound stays about
// as far behind the picture as at 30 fps.
namespace Gr2Port::SoundDelay {

/// Above 30 fps: shortens the game's pre-delay of sound effects and voices as the picture gains.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

} // namespace Gr2Port::SoundDelay
