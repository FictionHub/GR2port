// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"

namespace Xbyak {
class CodeGenerator;
}

// A layout animation's clock counts whole ticks of 1/3000 s and rounds each frame's time to a
// whole count. Above 30 fps a frame is a few ticks long, so the rounding changes the animation's
// speed: the 8.33 ticks of a 360 fps frame become 8, which is 4% slow, and the 12.5 ticks of a
// 240 fps frame become 13, which is 4% fast. The rounding takes the fps patch's running share of a
// 30 fps frame in place of one half, so the count follows the real time at every frame rate.
namespace Gr2Port::LayoutClock {

/// Hooks the rounding of a layout animation's step. Returns whether it went in.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

} // namespace Gr2Port::LayoutClock
