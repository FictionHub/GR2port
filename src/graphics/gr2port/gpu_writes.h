// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"

namespace Xbyak {
class CodeGenerator;
}

// The memory writes the game asks of its GPU. Nothing runs the game's command buffers, so each
// write the game puts in one, at the end of the pipe, at the end of a shader, inline or at a
// compute release, is made when the game puts it there, as by a GPU that finishes at once. The
// game's CPU waits on two of them: the draw thread on each context's frame counter before it
// reuses the context, and the movie decoder on its copy job's label.
namespace Gr2Port::GpuWrites {

/// Installs observers on the entries of the game's four writers.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

} // namespace Gr2Port::GpuWrites
