// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>

#include "common/types.h"

// The catcher's watcher outside the game: a second process of the same executable that the game
// starts, and that still stands when the game cannot speak for itself. It records how the game's
// process ended and whether it left a last word, and when the game's window or both of its own
// watchers stop answering it reads every thread's calls from outside. It writes to
// catcher_outside.txt beside the log.
namespace Gr2Port::Catcher::Outside {

/// The start option that makes a process the outside watcher of the process whose number follows.
inline constexpr const char* Option = "--catcher-outside";

/// What the game tells the outside watcher as it runs.
enum class Crumb : u32 {
    WatchBeat,
    GuardBeat,
    GameBase,
    GameFrames,
    RenderCall,
    LastWord,
    GpuStalled,
    Count,
};

/// The game, once, early: shares its state and starts the outside watcher. log_folder is where
/// the watcher writes.
void Start(const std::filesystem::path& log_folder);

/// The game, any thread: a crumb's value now. A beat's value is not used: its time is.
void Set(Crumb crumb, u64 value);

/// The outside watcher's whole run, for the game process with that number.
int Run(u32 process);

} // namespace Gr2Port::Catcher::Outside
