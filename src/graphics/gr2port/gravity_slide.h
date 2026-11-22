// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/types.h"

namespace Xbyak {
class CodeGenerator;
}

// The gravity slide's steering. Each frame the slide moves its steering a fraction of the way to
// the right stick, 0.04 for the normal style, and eases it back by an amount: at the PS4's 30
// frames a second the steering takes about two seconds to follow the stick. Both steps take the
// game step's time, so the steering follows the stick as fast at every frame rate.
// The slide's hold on the ground. Each frame the slide tests the ground at the point its velocity
// reaches in that frame, and sets the velocity to reach that point plus 0.3 or 0.4 of its gap to
// the ground. That share of a frame's gap is a speed that grows with the frame rate: where the
// ground drops away, as under a breaking panel of glass, a 360 fps step turns all of Kat's speed
// toward it. Above 30 fps the test looks as far ahead as in a 30 fps frame and the steps of such a
// frame share its change of speed.
// A panel of glass breaks a set time after Kat touches it, and the touch and the break each reach
// the slide a frame late. Above 30 fps those frames are shorter and the ground goes earlier under
// her, so the panel's time gains the difference.
namespace Gr2Port::GravitySlide {

/// Hooks the steering's two steps and, above 30 fps, the ground step and the glass's break time.
/// Returns whether they went in.
bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity);

} // namespace Gr2Port::GravitySlide
