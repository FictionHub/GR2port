// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"

namespace Xbyak {
class CodeGenerator;
}

// A Graphics Settings row at the bottom of the pause menu's System Menu, and the page it opens:
// GR2port's graphics settings, each a caption over its value, five on screen at a time, with Left
// and Right to change a value. Each row is one more of the System Menu's own rows, made with the
// game's recipe, and the page shows inside the System Menu page while that page reads no input.
// Changes go to config.json when the page closes, and the renderer takes those it can while the
// game runs; the game's own message box says when some take effect only at the next start.
namespace Gr2Port::GraphicsMenu {

using FindTextFunction = u64 PS4_SYSV_ABI (*)(u64 texts, const char* key);
using DrawFunction = void PS4_SYSV_ABI (*)(u64 handle, u64 context);

/// The start of the keys of the menu's texts.
inline constexpr char KeyPrefix[] = "gr2port_gfx_";

/// Adds the row and its page to the System Menu. ExitRows' text and draw hooks call FindText and
/// AfterDrawClip.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

/// The record of a key that starts with KeyPrefix, made from a record that find gives, or 0.
u64 FindText(u64 texts, const char* key, FindTextFunction find);

/// After the game drew a clip with context: draws the menu's rows that follow that clip.
void AfterDrawClip(u64 handle, u64 context, DrawFunction draw);

/// Renderer: the vendor of its GPU, which decides whether the page offers DLSS.
void SetGpuVendor(u32 vendor);

} // namespace Gr2Port::GraphicsMenu
