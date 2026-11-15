// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"

namespace Xbyak {
class CodeGenerator;
}

// The camera manager blends the gameplay cameras into its output each frame, its field of view
// first. The HUD projects with the output, the manager copies it into the camera parameter block,
// and the render view and most projections take the block's. A stub scales the output's field of
// view after the manager keeps the game's own for its next blend, while no event camera, a
// timeline's or a script's, holds the view, so the gameplay cameras widen or narrow and keep their
// own changes, such as the zoom of a gravity float. The scripts' queries of whether an object is
// in view still project with the game's own field of view: some scenes wait for a character to
// walk out of the game's view and would never end in a wider one.
namespace Gr2Port::FieldOfView {

/// Hooks the camera manager's output and the projection of the scripts' view queries, with the
/// field of view of the settings.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

/// Any thread: the gameplay cameras' field of view in percent of the game's own, from the next
/// frame.
void Set(u32 percent);

} // namespace Gr2Port::FieldOfView
