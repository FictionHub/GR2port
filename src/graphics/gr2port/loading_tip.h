// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"
#include "graphics/gr2port/graphics_menu.h"

namespace Xbyak {
class CodeGenerator;
}

// A line in the game's own font at the bottom of the black screen that loads a save from the title
// menu, to the left of the game's loading icon.
namespace Gr2Port::LoadingTip {

/// The start of the key of the tip's text.
inline constexpr char KeyPrefix[] = "gr2port_tip_";

/// Hooks the loading icon's draw.
void Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

/// The record of a key that starts with KeyPrefix, made from a record that find gives, or 0.
u64 FindText(u64 texts, const char* key, GraphicsMenu::FindTextFunction find);

} // namespace Gr2Port::LoadingTip
