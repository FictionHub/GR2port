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
// fractions take the real frame's time, which slow motion and a stopped game clock leave alone, so
// the move takes as long at every frame rate. On a comic's last page the menu waits 20 frames for
// the panel's animation to start before it can end the comic, and that wait takes the same time at
// every frame rate too.
namespace Gr2Port::ComicCamera {

/// Hooks the move's frame counts, both of its fractions and the last page's wait. They read the
/// fps patch's frame time, so they go in only above 30 fps. Returns whether all hooks went in.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

} // namespace Gr2Port::ComicCamera
