// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "common/types.h"
#include "graphics/gr2port/scene.h"

namespace Xbyak {
class CodeGenerator;
}

// The game's screen drawing, read from the game's 2D items. Each frame the game's draw thread
// (FUN_0084b700) runs a list of draw items in 512 layers: layout elements and their text, the
// menus' text, the HUD glow's passes, the screen's fade, movies, the captured background and the
// photo. As each item comes up, before its own function runs, UiItems reads it, and the engine
// objects it points to (materials, vertex buffers, index buffers, textures, matrices, colours),
// into Draws in the order the game draws them. ScreenDraw's own copies, blurs and colour control
// come from the engine functions it calls for them.
namespace Gr2Port::UiItems {

/// A vertex input: the address of the first vertex, the stride, the buffer's format word (the
/// last word of the engine's buffer descriptor) and the vertices the buffer holds.
struct Input {
    VAddr address = 0;
    u32 stride = 0;
    u32 format = 0;
    u32 elements = 0;
};

/// A texture of an engine texture or target object: its T# (8 words) and S# (4 words).
struct Texture {
    VAddr sharp = 0;
    VAddr sampler = 0;
};

/// A draw of the screen drawing.
struct Draw {
    Scene::UiVertexProgram vertex{};
    Scene::UiPixelProgram pixel{};
    /// The engine target object it draws into: + 0x14 width, + 0x16 height, + 0x30 T#.
    VAddr target = 0;
    /// CB_BLEND0_CONTROL as the renderer takes it.
    u32 blend = 0;
    u32 topology = Scene::UiPrimitiveList;
    /// The viewport's x scale and offset, then its y scale and offset.
    std::array<float, 4> viewport{};
    /// The inputs, by the vertex program's input slots.
    std::array<Input, Scene::UiAttributes> inputs{};
    /// count indices of index_bits bits from first on at indices; or, without indices, count
    /// vertices.
    VAddr indices = 0;
    u32 index_bits = 16;
    u32 first = 0;
    u32 count = 0;
    /// Constants, each an address and its bytes, in the game's memory or the frame's arena.
    VAddr vs_constants = 0;
    u32 vs_bytes = 0;
    VAddr vs_palette = 0;
    u32 palette_bytes = 0;
    VAddr ps_constants = 0;
    u32 ps_bytes = 0;
    std::array<Texture, 2> textures{};
    /// Scene::UiDraw flags the item itself gives.
    u32 flags = 0;
    /// The draw stays while the screen drawing over the picture is hidden.
    bool kept = false;
    /// The item's virtual table, for the logs; 0 for ScreenDraw's own draws.
    VAddr item = 0;
};

/// When the executable loads, with its base address. Stubs are written by c within capacity bytes
/// of its buffer.
void Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

/// Draw thread, in place of the call of a draw item's function: reads the item, then runs it.
/// kept: the item's draws stay while the screen drawing over the picture is hidden.
void RunItem(VAddr item, bool kept);

/// Draw thread: the frame's draws so far.
std::span<const Draw> Draws();

/// Draw thread, once the frame's draws are written: starts the next frame's.
void Clear();

} // namespace Gr2Port::UiItems
