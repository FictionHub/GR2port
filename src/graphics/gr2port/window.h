// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>

#include "common/types.h"

struct SDL_Window;

// The game's window, the only one: the emulator takes its input from it and the renderer draws in
// it. SDL creates and changes windows on the main thread only.
namespace Gr2Port::Window {

struct Size {
    u32 width;
    u32 height;
};

/// Main thread, at the emulator's start: creates the window, hidden. Returns null on failure.
SDL_Window* Open();

/// Any thread but the main one: shows the window as the settings choose it, waits until the main
/// thread has done so, and returns it. Returns null without a window.
SDL_Window* Show();

/// The window's size in pixels as of its last resize. Any thread.
Size PixelSize();

/// The window's SDL id, or 0 before Open. Any thread.
u32 Id();

/// The sizes of the display modes of the window's monitor, from the smallest, as the launcher
/// lists them, as of the window's show or its last change of monitor or mode. Any thread.
std::vector<Size> MonitorSizes();

/// Any thread: shows the window as the settings choose it now, on the monitor it is on, when
/// their fullscreen or size differ from the ones it last took.
void Apply();

/// Main thread: switches the window between windowed and fullscreen as the settings choose it, on
/// the monitor it is on.
void ToggleFullscreen();

/// Any thread: if free is true, shows the cursor and lets it move over the window, where a
/// prompt's buttons take the mouse; otherwise hides and holds it again for the camera.
void FreeCursor(bool free);

} // namespace Gr2Port::Window
