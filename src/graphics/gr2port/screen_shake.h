// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"

namespace Xbyak {
class CodeGenerator;
}

// Each frame the camera manager adds the shakes the game started (landings, the shift's boost,
// attacks, scripts) into an offset and a turn. The gameplay camera and the event cameras move their
// eye by the offset and their target by 1.5 times it, and turn their up vector by the turn. While
// the pad's vibration is linked to the camera (a script's setPadVibLink; gameplay keeps it linked)
// the pad rumbles by the offset's size. Stubs scale the offset and the turn where the cameras take
// them, so the pad rumbles as with the game's own shake.
namespace Gr2Port::ScreenShake {

/// Hooks the gameplay and event cameras' shake, with the screen shake of the settings.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

/// Any thread: the cameras' shake in percent of the game's own, from the next frame.
void Set(u32 percent);

} // namespace Gr2Port::ScreenShake
