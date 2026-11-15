// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

#include "common/types.h"

// The call stacks of the process's threads, in words: a function's name where the executable or a
// system file gives one, the game's own addresses as its patches name them.
namespace Gr2Port::Catcher::Stacks {

/// When the game's executable is loaded at base.
void SetGame(VAddr base);

/// A line for every other thread of the process, with its name and its calls from the innermost
/// out. Each thread stops only while its stack is read. Not from a thread that holds the log.
std::vector<std::string> All();

/// The calls of the thread whose registers context holds, a CONTEXT on Windows, from the
/// innermost out. For a thread that is stopped, or the caller's own from a fault's handler.
std::string Of(const void* context);

/// The name of the code at address.
std::string Name(u64 address);

/// From another thread, when All has taken longer than it should: lets the thread it stopped run
/// again and makes All give up. True when All was running.
bool Rescue();

/// Whether the executable carries its functions' names.
bool Named();

} // namespace Gr2Port::Catcher::Stacks
