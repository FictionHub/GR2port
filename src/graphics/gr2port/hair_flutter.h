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
// The float's push along Kat's hair, the swirl that Kat's update pushes it by in some states and
// the push of her Jupiter and Lunar styles stay the same in every frame instead, so their air
// would grow with the frame rate and keep the hair above the limit. Four more stubs scale them
// the same way.
// The wave's push is a speed the node keeps, less the node's drag in each step. A 30 fps step
// follows a fast wave poorly, while smaller and more frequent steps follow it closely, so in a
// strong wind the hair swings several times as far. A last stub scales the push down to the
// swing a 30 fps step gives the wave.
namespace Gr2Port::HairFlutter {

/// Scales the hair's flutter limit and the float's, Kat's swirl's and her styles' pushes on the
/// hair by the game step's time, and the wave's push to the hair's swing at 30 fps.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

} // namespace Gr2Port::HairFlutter
