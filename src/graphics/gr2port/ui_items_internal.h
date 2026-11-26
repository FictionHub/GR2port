// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstring>

#include "common/types.h"
#include "graphics/gr2port/hooks.h"
#include "graphics/gr2port/ui_items.h"

// What the readers of the game's 2D items share. Draw thread only.
namespace Gr2Port::UiItems {

/// The executable's base address.
extern VAddr g_base;

/// A value of the game's memory.
template <typename T>
T Load(VAddr address) {
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}

/// The game's address of a GoldHEN address.
inline VAddr Game(VAddr address) {
    return g_base + address - Hooks::CodeBase;
}

/// Adds a draw of the current item, with its kept state and virtual table.
void Add(Draw draw);

/// Copies bytes into the frame's arena and gives their address there, 0 when there is no room.
VAddr Keep(const void* data, u32 bytes);

/// The target the item walk draws into now, and setting it, as the engine's target setter
/// FUN_016137a0 does for the items that set one.
VAddr Target();
void SetTarget(VAddr target);

/// A target object's whole-target viewport, as FUN_016137a0 sets it.
std::array<float, 4> Viewport(VAddr target);

/// The game's blend modes (FUN_01509d90's mode: 1 premultiplied, 2 add, 3 multiply, 4, 5, 6),
/// as CB_BLEND0_CONTROL; anything else is no blending.
u32 Blend(s32 mode);

/// The readers of the item kinds, each given the item.
using Reader = void (*)(VAddr item);

/// The readers each part defines, by virtual table: one before the item's function runs, and
/// optionally one after it, for what the function makes as it runs.
struct Kind {
    VAddr vtable;
    Reader read;
    Reader after = nullptr;
};
std::span<const Kind> ElementKinds();
std::span<const Kind> TextKinds();
std::span<const Kind> PassKinds();

/// The screen drawing's own draws: FUN_0150aba0(blur, source, mode), and the colour control after
/// the walk.
void Background(float blur, VAddr source, u32 mode);
void ColorControl();

} // namespace Gr2Port::UiItems
