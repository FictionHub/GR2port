// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"

namespace Xbyak {
class CodeGenerator;
}

// A chain of the game's type Himo, which also makes up some characters' hair, has a spring between
// each node and the node after next that moves them by a fixed share of the gap in every step. The
// spring keeps the chain bent, and the bend turns about the chain a fixed amount per step, so above
// 30 fps the hair whirls as much faster as the frame rate is higher. The spring's share follows
// the real time of the frame.
namespace Gr2Port::CordBend {

/// Hooks the Himo chain's spring. Returns whether it went in.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

} // namespace Gr2Port::CordBend
