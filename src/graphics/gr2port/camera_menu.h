// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <string_view>

#include "common/types.h"

namespace Xbyak {
class CodeGenerator;
}

// Two more sliders on the System Menu's Camera Configuration page, after the game's Camera
// Sensitivity: the camera sensitivity of the Special Attack's flight and of Panther Mode, which the
// game's own leaves alone. The page's two slider places show the four sliders two at a time, with
// a scroll bar beside them, and Up and Down move through them as through a list. Each new slider is
// one more of the game's own slider rows, made with the game's recipe; its value turns the camera
// at once and goes to config.json when the page closes. GraphicsMenu, which holds the System
// Menu's hooks, calls the functions below.
namespace Gr2Port::CameraMenu {

using DrawFunction = void PS4_SYSV_ABI (*)(u64 handle, u64 context);

/// The start of the keys of the sliders' captions, before the English caption, and the key of
/// the record they take their style from: the game's Camera Sensitivity.
inline constexpr std::string_view KeyPrefix = "gr2port_gfx_cam_";
inline constexpr char ModelKey[] = "ui_system_menu_1_5_2";

/// Hooks the camera page's frame.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

/// As the System Menu makes its rows: makes the sliders for system.
void MakeRows(u64 system);

/// Whether the sliders loaded, or there are none to wait for.
bool RowsLoaded();

/// Once the System Menu placed its rows: hangs the sliders, hidden, on the page.
void Hang(u64 system);

/// After the System Menu's rows took a slide: the sliders take it too.
void Slide(u64 system, u32 mode);

/// After the game drew the clip the System Menu's own rows draw after: draws the sliders.
void Draw(u64 system, u64 context, DrawFunction draw);

/// Before the System Menu ends its rows: ends the sliders.
void End(u64 system);

} // namespace Gr2Port::CameraMenu
