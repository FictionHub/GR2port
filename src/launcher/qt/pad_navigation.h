// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QTimer>

namespace Gr2Port::Launcher {

/// While it lives, the gamepads move through the application's windows as the keyboard does: the
/// menu takes the arrows and Enter, and the dialogs move between their widgets, change choices
/// and tabs, press buttons, and close as Escape closes them.
class PadNavigation {
public:
    PadNavigation();
    ~PadNavigation();
    PadNavigation(const PadNavigation&) = delete;
    PadNavigation& operator=(const PadNavigation&) = delete;

private:
    QTimer timer;
};

} // namespace Gr2Port::Launcher
