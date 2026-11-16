// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace Gr2Port {

/// Logs whether an overlay's hooks are in the process.
void LogOverlays();

/// Render thread, at start and now and then: once RTSS's hook is in the process, its opens of
/// display adapters other than the renderer's, named by PCI vendor and device, fail at once
/// instead of reaching Windows.
void KeepOverlayOffOtherAdapters(u32 vendor, u32 device);

} // namespace Gr2Port
