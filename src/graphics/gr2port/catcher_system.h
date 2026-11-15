// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

#include "common/types.h"

// What the system around the process knows about the GPU: Windows's time-out and scheduling
// settings, the drivers' and Windows's own records of GPU errors, and a running minute of each
// GPU's load, the NVIDIA GPU's temperature, power and clock limits, and the process's memory.
// Every part is asked of the system when the process runs, and a part the system lacks only
// leaves its lines out.
namespace Gr2Port::Catcher::System {

/// Once: the settings, and the records of the last day.
std::vector<std::string> AtStart();

/// Once, with the device made: starts the thread that samples the load.
void StartSampling();

/// The last minute's samples in short, the power and processor state now, and the records of
/// the last minutes minutes.
std::vector<std::string> AtEvent(u32 minutes);

/// One line: each GPU's load from this process and from all, now.
std::string LoadNow();

} // namespace Gr2Port::Catcher::System
