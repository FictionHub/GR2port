// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"

namespace Xbyak {
class CodeGenerator;
}

// The game makes each effect in FUN_014a6990 from the hash of its file's name. With the smoke and
// dust off, plays of the files of SmokeEffects::Files get the empty name's hash there, for which
// the game makes no effect. Loads into the effect registry keep the hash: an entry without an
// effect stays loading, and a challenge race against several ghosts waits on such entries.
namespace Gr2Port::SmokeAndDust {

/// With the settings' smoke and dust off, puts the switch in front of the game's effect spawn.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

} // namespace Gr2Port::SmokeAndDust
