// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "common/types.h"

// The catcher's history: the last things that happened around the game, each with its time and
// the game's frame, which a report prints so that a fault stands beside what came before it. For
// code that needs nothing else of the catcher.
namespace Gr2Port::Catcher {

/// Any thread: what happened, of at most 63 characters, with a number of its own.
void Note(std::string_view what, u64 value);

/// The pad library, as the game reads pad number pad: notes the first pad's buttons when they
/// change, so the history holds the presses beside what the game did.
void NotePad(int pad, u32 buttons);

/// The history, oldest first, each line with how long before now it was.
std::vector<std::string> History();

} // namespace Gr2Port::Catcher
