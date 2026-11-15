// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"

namespace Xbyak {
class CodeGenerator;
}

// Hair is a chain of nodes that the game steps once a frame. The air, the gravity and Kat's own
// motion reach the step as distances in that frame, which the game scales by the frame's time.
// When the air moves along a chain by more than the chain's limit in a frame, the step
// FUN_01501b20 makes the hair flutter by the excess: it pushes the middle node sideways by a
// wave and advances the wave's phase by the excess. The limit is made for a 30 fps frame, so
// above 30 fps the air stays under it and the hair stops fluttering. One stub here scales the
// limit by the game step's time in 30 fps frames, which is 1 at 30 fps. The excess then scales
// with the frame's time as well, and so do the wave's push and its phase's advance.
// The float's push along Kat's hair stays the same in every frame instead, so the air of a float
// would grow with the frame rate. The other stub scales it the same way.
namespace Gr2Port::HairFlutter {

/// Scales the hair's flutter limit and the float's push on the hair by the game step's time.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

} // namespace Gr2Port::HairFlutter
