// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"

namespace Xbyak {
class CodeGenerator;
}

// Gravity Rush 2's menus, title screen and comic scenes turn with the pad's tilt alone, through
// FUN_01397200. When player 1's pad has no gyro, or there is no pad, the right stick tilts them
// too: full deflection tilts them as far as they go. In those screens the mouse moves the right
// stick. A pad with a gyro tilts them as the game does.
namespace Gr2Port::StickTilt {

/// Replaces the game's tilt with the one that adds the right stick.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

} // namespace Gr2Port::StickTilt
