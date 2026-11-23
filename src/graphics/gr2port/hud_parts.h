// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"
#include "graphics/gr2port/settings.h"

// The parts of Gravity Rush 2's HUD the graphics settings can hide. Each part is a HUD object whose
// drawing function returns at once while the part is hidden; its update still runs, so the part
// shows its current state again when it comes back.
namespace Gr2Port::HudParts {

/// At the executable's load: takes the stored settings.
void Install(VAddr module_base);

/// Any thread, while the game runs: hides or shows the parts as the settings say.
void Choose(const Settings::Graphics& graphics);

} // namespace Gr2Port::HudParts
