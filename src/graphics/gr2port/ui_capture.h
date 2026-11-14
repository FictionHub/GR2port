// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"
#include "graphics/gr2port/scene.h"

// The game's screen drawing: after the frame graph, its draw thread draws the menus, the HUD, text
// and movies from a list of objects. Each object sets its state and draws through the engine, so
// this reads what they all did from the game's commands for that part of the frame.
namespace Gr2Port::UiCapture {

/// When the executable loads, with its base address.
void Install(VAddr module_base);

/// On the game's draw thread, when the frame graph returns: notes where the screen drawing begins
/// and the state it begins with.
void OnFrameGraphEnd();

/// On the game's draw thread, when it changes to its next context: writes the frame's screen
/// drawing to the packet at address, which is all of its commands when the frame graph did not
/// run. world is the target object the frame graph drew the world's picture into, 0 for the back
/// buffer.
void Write(u8* packet, u64 address, Scene::Header& header, VAddr world);

/// Instead of Write, for a frame without a packet.
void Skip();

/// A scroll bar on the game's 1920 x 1080 canvas, whose y grows downwards: a track around x from
/// top to bottom, of the width, and the thumb on it.
struct ScrollBar {
    float x = 0.0f;
    float width = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
    float thumb_top = 0.0f;
    float thumb_bottom = 0.0f;
    float opacity = 0.0f;
};

/// Any thread: the scroll bar the screen drawing of the frames from here on ends with. A bar of
/// no opacity draws nothing, and neither does one that is not set again for a while.
void SetScrollBar(const ScrollBar& bar);

} // namespace Gr2Port::UiCapture
