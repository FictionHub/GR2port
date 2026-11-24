// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The gamepads move through the launcher's windows as the keyboard does: the menu takes the arrows
// and Enter, the dialogs move between their controls, change choices and tabs, and press buttons,
// and other windows over them, such as task dialogs and the folder picker, take typed keys.
namespace Gr2Port::Launcher::PadNavigation {

/// Starts SDL's gamepads and a timer of the calling thread that hands their commands to its
/// window in front. Stop ends both.
void Start();
void Stop();

} // namespace Gr2Port::Launcher::PadNavigation
