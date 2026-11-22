// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"

namespace Xbyak {
class CodeGenerator;
}

// FUN_00bb6080 gives a hit to a body part of an enemy, and the enemy's health follows the hit
// points of its parts. A part made by FUN_00ffef60 has no pause between hits, so an attack that
// stays in contact with it, as a beam does, hits on every game frame and its damage grows with the
// frame rate. Above 30 fps such a contact lands 30 hits a second, as at the game's own frame rate.
// A hit that does not follow the same attack's hit on the previous frame is a separate shot and
// always lands. A hit that does not land leaves the attack the result of the last one that did, so
// the attack behaves as it does on a frame with a hit.
namespace Gr2Port::PartHits {

/// Hooks the function that gives a hit to a body part. Returns whether it went in.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

} // namespace Gr2Port::PartHits
