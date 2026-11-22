// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"

namespace Xbyak {
class CodeGenerator;
}

// A script moves an object by setting its world position once per 30 fps frame. In a frame the
// camera reads that position, then the physics pulls the object's body toward it and leaves the
// object where the body went. Above 30 fps only some frames have a position from the script. On the
// others the camera aims at the body, which stops following and falls, so the camera and the
// object jump between the two. Until the script's next position, each frame gives such an object
// the position a script would have set, eased toward the script's latest one over a little more
// than the real time of a 30 fps frame. The camera reads that position, and the body then goes
// toward it at the speed it has at 30 fps.
namespace Gr2Port::ScriptedMoves {

/// Hooks the script function that sets a world position, and the game's frame after its scripts
/// and after its camera. Returns whether all three went in.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

} // namespace Gr2Port::ScriptedMoves
