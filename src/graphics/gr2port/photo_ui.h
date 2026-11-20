// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"
#include "graphics/gr2port/graphics_menu.h"

namespace Xbyak {
class CodeGenerator;
}

// Gravity Rush 2's photo mode draws its dark border, the viewfinder's grid and its control lists
// in the screen drawing over the view through Kat's camera. A press of R3 while the camera is out
// hides all of the screen drawing over the picture, and another press shows it again. Putting the
// camera away shows it again too. The left control list shows R3 and Hide UI under its Filter
// Effect row, in a row of the game's own.
namespace Gr2Port::PhotoUi {

/// The start of the key of the Hide UI row's text.
inline constexpr char KeyPrefix[] = "gr2port_photo_";

/// At the executable's load.
void Install(VAddr module_base);

/// Adds the Hide UI row to the control list. ExitRows' text hook calls FindText.
bool AddRow(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

/// The record of a key that starts with KeyPrefix, made from a record that find gives, or 0.
u64 FindText(u64 texts, const char* key, GraphicsMenu::FindTextFunction find);

/// The game's pad reads, with each state's PlayStation buttons in order: takes the presses of R3
/// on the pad of handle while the camera is out.
void OnButtons(s32 handle, u32 buttons);

/// Any thread: whether the screen drawing over the picture is hidden.
bool Hidden();

} // namespace Gr2Port::PhotoUi
