// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>

#include "common/types.h"

// The period of the vblank that paces the game's frames. While presents wait for the monitor and
// the monitor's refresh rate is within a hundredth of the frame rate, or of a whole multiple or
// fraction of it, the vblank runs at the monitor's rate: game frames and refreshes stay in step.
namespace Gr2Port::Vblank {

/// Main thread: the refresh rate of the monitor the window is on, in Hz, or 0 when unknown.
void SetRefreshRate(double rate);

/// Render thread: whether the swapchain's presents wait for the monitor's refresh.
void SetSynced(bool synced);

/// Any thread: the vblank's period for the game's frame rate.
std::chrono::nanoseconds Period(u32 frame_rate);

} // namespace Gr2Port::Vblank
