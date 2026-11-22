// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"

namespace Xbyak {
class CodeGenerator;
}

// Short windows in the game's code that assume a frame of 30 fps. Two counts of frames, how long
// an enemy has stood on a moving platform and how long Dusty has followed one, advance only on
// frames that complete a 30 fps frame. A run's ledge window ends on the frame nearest its length,
// and the job queues' time while the world is stopped is the length of the frame.
namespace Gr2Port::FrameWindows {

/// Above 30 fps: hooks the windows. Returns whether all went in.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

} // namespace Gr2Port::FrameWindows
