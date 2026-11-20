// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"
#include "graphics/gr2port/memory.h"
#include "graphics/gr2port/scene.h"

// Screenshots: the frame's back buffer, the game's picture with its screen drawing and without
// GR2port's own windows over it, saved as a PNG in the user folder's screenshots folder.
namespace Gr2Port::Screenshot {

/// Any thread: asks for a screenshot of the next frame the renderer draws.
void Request();

/// On the render thread, after the screen drawing: copies the frame's back buffer when a
/// screenshot is asked for.
void Record(Memory& memory, vk::CommandBuffer cmd, const Scene::Header& header, u32 slot);

/// On the render thread, once the slot's commands have run: saves the screenshot they copied.
void Collect(u32 slot);

} // namespace Gr2Port::Screenshot
