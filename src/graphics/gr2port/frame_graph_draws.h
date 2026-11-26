// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"

namespace Xbyak {
class CodeGenerator;
}

// The frame graph's draws and dispatches, which the game no longer writes. No renderer draws the
// game's command buffers and there is no command processor: Gr2Port::GpuWrites makes their fences,
// and a flip goes to the video out at its submit. The game's per-item draw loop returns at once,
// and the draws and dispatches the frame graph asks for are dropped: they are for the world's
// picture, which nothing reads. The screen drawing after the frame graph is read from its draw
// items (UiItems), and the writers of its programs, registers and draws return at once
// (UiCapture). The room the game takes in its command buffers for labels and constants holds them
// alone, without a packet.
namespace Gr2Port::FrameGraphDraws {

/// When the executable loads, with its base address. Stubs are written by c within capacity bytes
/// of its buffer.
void Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

/// On the game's draw thread: if running is true, the frame graph begins; otherwise it has
/// returned.
void SetFrameGraphRunning(bool running);

/// On the game's draw thread: whether the frame graph runs.
bool FrameGraphRunning();

} // namespace Gr2Port::FrameGraphDraws
