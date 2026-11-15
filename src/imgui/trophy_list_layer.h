// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The trophy list in the game: the progress and the trophies of the trophy files in the user
// folder, over the game. While it is open the game reads a neutral pad and the system reports an
// overlay on the screen, as a PS4 does under its own menus.
namespace Gr2Port::TrophyList {

/// Opens the trophy list, or closes it when it is open.
void Toggle();

/// Whether the trophy list is open, from any thread.
bool IsOpen();

} // namespace Gr2Port::TrophyList
