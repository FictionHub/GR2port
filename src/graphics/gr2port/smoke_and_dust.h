// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"

namespace Xbyak {
class CodeGenerator;
}

// The game makes each effect in FUN_014a6990 from the hash of its file's name. With the smoke and
// dust off, the files of SmokeEffects::Files get the empty name's hash there, for which the game
// makes no effect. The game then marks such an effect failed and asks no more, so the setting
// holds from the start of a run.
namespace Gr2Port::SmokeAndDust {

/// With the settings' smoke and dust off, puts the switch in front of the game's effect spawn.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

} // namespace Gr2Port::SmokeAndDust
