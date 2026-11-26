// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"
#include "graphics/gr2port/settings.h"

namespace Xbyak {
class CodeGenerator;
}

// How far the sun's shadows reach. Each frame FUN_01421f60 copies the scene's shadow distances
// from its parameters: where the far shadow map ends, and how far objects of casting mode 2 cast.
// FUN_014de070 then drops a caster past 600 m, and one of casting mode 0 past 20 times its radius.
// Shadow Draw Distance scales every casting distance, and above 100% the end too. The lower shadow
// qualities scale the casting distances down again, so smaller and farther objects cast no shadow.
namespace Gr2Port::ShadowDistance {

/// Hooks the copy of the scene's shadow distances and the two casting tests, at the distance of
/// Settings::Graphics::shadow_distance and the casting share of its shadow quality.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

/// Any thread: the casting share of quality from the game's next frame on.
void SetQuality(Settings::Quality quality);

} // namespace Gr2Port::ShadowDistance
