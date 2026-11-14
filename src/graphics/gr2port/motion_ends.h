// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"

namespace Xbyak {
class CodeGenerator;
}

// FUN_008f6430 advances the playheads of Kat's motions by the game step in 1/3000 s ticks,
// rounded. A motion that plays once stops at its length and the step's remainder is dropped, so
// the frame that reaches the end shows less movement than the others. Kat's run plays each stride
// as such a motion of 800 ticks and starts the next one on the frame after: at 30 fps the strides
// end on a frame, and above 30 fps the stall varies from stride to stride with the game step. The
// stub here splits the rest of a motion that plays once into equal whole steps nearest the game
// step, so it ends on a frame as at 30 fps.
namespace Gr2Port::MotionEnds {

/// Makes motions that play once end on a frame.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

} // namespace Gr2Port::MotionEnds
