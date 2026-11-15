// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>

// The ways a process dies by its own hand, each given a last word in the log: a fault nothing
// handles, a terminate, an abort, a call with a bad argument, a call of a pure function, memory
// that runs out, and an orderly exit. The word is what happened, where, the calls that led there
// and what every other thread was in.
namespace Gr2Port::Catcher::Crash {

/// Once, early: installs the handlers. Those that were there before still run after. The last
/// words also go to a file of their own in log_folder, which the next run's log repeats.
void Install(const std::filesystem::path& log_folder);

/// Any thread, now and then: other code takes the abort signal for itself after Install, so this
/// takes it back and keeps theirs to run after. Each thread has its own terminate handler, so
/// this also gives the calling thread the catcher's.
void Keep();

} // namespace Gr2Port::Catcher::Crash
