// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"

namespace Xbyak {
class CodeGenerator;
}

// Gravity Rush 2's menus and title screen turn with the pad's tilt alone, through FUN_01397200,
// and its comic pages through their own copy of that tilt in FUN_017772a0. When player 1's pad has
// no gyro, or there is no pad, the right stick tilts them too: full deflection tilts them as far
// as they go. In those screens the mouse moves the right stick. A pad with a gyro tilts them as
// the game does.
namespace Gr2Port::StickTilt {

/// Replaces the game's tilts with the ones that add the right stick.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

/// Any thread: whether a comic page ran its update in the last 200 ms. The mouse then moves the
/// right stick, which tilts the page, and no camera behind the page takes the mouse.
bool ComicPageShown();

} // namespace Gr2Port::StickTilt
