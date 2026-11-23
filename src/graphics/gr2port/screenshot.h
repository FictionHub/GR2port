// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"
#include "graphics/gr2port/memory.h"
#include "graphics/gr2port/scene.h"

// Screenshots: the frame's back buffer, the game's picture with its screen drawing and without
// GR2port's own windows over it, saved as a PNG in the user folder's screenshots folder. Taking one
// plays the camera shutter of the game's photo mode, from the game's own sounds.
namespace Gr2Port::Screenshot {

/// Once the game's code is the expected code: lets a screenshot play the shutter.
void Install();

/// Any thread: asks for a screenshot of the next frame the renderer draws, and for its shutter.
void Request();

/// On the game's main thread, at each pad read: plays the shutter of a screenshot asked for.
void PlayShutter();

/// On the render thread, after the screen drawing: copies the frame's back buffer when a
/// screenshot is asked for.
void Record(Memory& memory, vk::CommandBuffer cmd, const Scene::Header& header, u32 slot);

/// On the render thread, once the slot's commands have run: saves the screenshot they copied.
void Collect(u32 slot);

} // namespace Gr2Port::Screenshot
