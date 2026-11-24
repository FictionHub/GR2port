// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>

// The gamepads' commands for the launcher's windows, as the game's menus take them: the D-pad or
// the left stick moves, the south button (A, cross) accepts, the east button (B, circle) goes
// back, and the shoulder buttons change tabs.
namespace Gr2Port::Launcher::PadCommands {

enum class Command {
    Up,
    Down,
    Left,
    Right,
    Accept,
    Back,
    PreviousTab,
    NextTab,
};

/// Starts SDL's gamepads for the launcher. Stop lets them go before the game starts them again
/// with its own hints.
void Start();
void Stop();

/// Looks at the pads, then returns the oldest command not yet taken: a button's press, or a
/// direction's, which repeats while it is held.
std::optional<Command> Next();

/// While a Pause lives, the pads give no commands, for a dialog that reads them itself. A button
/// or direction still held when the Pause ends gives none until it is let go.
class Pause {
public:
    Pause();
    ~Pause();
    Pause(const Pause&) = delete;
    Pause& operator=(const Pause&) = delete;
};

} // namespace Gr2Port::Launcher::PadCommands
