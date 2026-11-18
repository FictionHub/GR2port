// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string_view>

struct SDL_Window;

// A bad dump plays to the end of Episode 4 and then hangs on a black screen after the movie of the
// Banga fleet's arrival at Jirga Para Lhao. When the story plays that movie and the game draws no
// world in the minute after it closes, the player is told to dump the game again and the game
// closes. The title screen plays movies the save has seen behind its menus, the arrival among
// them: a play of the arrival while the title screen's own movie is open is the title screen's.
namespace Gr2Port::GameCheck::ArrivalHang {

/// Game thread: a movie player opened the movie at path.
void OnMovieOpened(const void* player, std::string_view path);

/// Game thread: a movie player closed.
void OnMovieClosed(const void* player);

/// Draw thread: the frame graph runs, so the frame draws the world.
void OnWorldFrame();

/// Main thread, on SDL_EVENT_BAD_DUMP: tells the player the game files are bad.
void Tell(SDL_Window* window);

} // namespace Gr2Port::GameCheck::ArrivalHang
