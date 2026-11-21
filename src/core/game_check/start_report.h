// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace Gr2Port::GameCheck {

/// Keeps how the executable was started: the program that started it, from a terminal or not, and
/// its options as given. Call first in main, after the terminal is attached and before any option
/// is read or taken out.
void RememberStart(int argc, char* argv[]);

/// Logs how the executable was started and its options, once the log is set up.
void LogLaunch();

/// Logs how the executable was started, its options and every setting in use, once the game's
/// log has begun.
void LogStart();

} // namespace Gr2Port::GameCheck
