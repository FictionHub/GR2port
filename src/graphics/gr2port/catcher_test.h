// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"
#include "graphics/gr2port/device.h"

namespace Gr2Port {
class Memory;
}

// Faults made on purpose, to see that the catcher catches each kind. The environment variable
// GR2PORT_CATCHER_TEST names one, "name" or "name,frame", with the game frame to make it at:
// stall and long-stall keep the GPU busy in the water pass, freeze stops the game's draw thread,
// render stops the render thread, watch stops the catcher's own watch, crash, abort, terminate
// and exit end the process from the render thread, kill ends it with no word at all, and
// bad-water gives the water pixel program a screen size that sends its light grid reads outside
// the grid, and guard-test makes that program read outside for certain. Both draw the water
// through what hides it.
namespace Gr2Port::Catcher::Test {

/// Render thread, once.
void Start(Memory& memory);

/// The game's draw thread, as each of its frames begins.
void Game(u64 frame);

/// Render thread, as it begins the frame that draws game_frame.
void Render(u64 game_frame);

/// Render thread, outside a rendering, as the water pass begins.
void Gpu(const Device& device, vk::CommandBuffer cmd, u64 game_frame);

/// The catcher's watch, each time it looks.
void Watch();

/// The game's draw thread, with a water draw's pixel constants in the packet, written already.
void Water(u32* pixel_constants, u64 frame);

/// Whether the water pixel program makes its guard's test read: from the guard-test fault on.
bool GuardTest();

/// Whether the water draws without its depth test: from either water fault on.
bool HiddenWater();

} // namespace Gr2Port::Catcher::Test
