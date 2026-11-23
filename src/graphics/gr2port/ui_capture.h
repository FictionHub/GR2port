// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"
#include "graphics/gr2port/scene.h"

namespace Xbyak {
class CodeGenerator;
}

// The game's screen drawing: after the frame graph, its draw thread draws the menus, the HUD, text
// and movies from a list of objects. Each object sets its state and draws through the engine, whose
// writers of that state and of the draws this replaces, keeping each draw with its state.
namespace Gr2Port::UiCapture {

/// When the executable loads, with its base address: in place of the engine's writers of the
/// screen drawing's state and draws. Stubs are written by c within capacity bytes of its buffer.
void Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

/// On the game's draw thread, when the frame graph returns: notes the state the screen drawing
/// begins with.
void OnFrameGraphEnd();

/// On the game's draw thread, when it changes to its next context: writes the frame's screen
/// drawing, the draws since the last Write or Skip, to the packet at address. world is the target
/// object the frame graph drew the world's picture into, 0 for the back buffer.
void Write(u8* packet, u64 address, Scene::Header& header, VAddr world);

/// Instead of Write, for a frame without a packet.
void Skip();

/// On the draw thread, around one of the game's draw items: the draws it makes between the two
/// calls stay while the screen drawing over the picture is hidden.
void BeginKept();
void EndKept();

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
