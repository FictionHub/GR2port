// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"

namespace Xbyak {
class CodeGenerator;
}

// The camera of a 3D comic page. After a panel change the camera moves a fraction of the way to
// the new panel each frame, 0.2 by default, and from the 15th frame that fraction grows to 1 over
// 10 frames: at the PS4's 30 frames a second the move takes about 0.8 s. The frame counts and the
// fractions take the game step's time, so the move takes as long at every frame rate.
namespace Gr2Port::ComicCamera {

/// Hooks the move's frame counts and both of its fractions. Returns whether all hooks went in.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

} // namespace Gr2Port::ComicCamera
