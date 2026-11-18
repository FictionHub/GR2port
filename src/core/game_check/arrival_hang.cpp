// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_messagebox.h>

#include "common/logging/log.h"
#include "core/game_check/arrival_hang.h"
#include "core/language/language.h"
#include "input/input_handler.h"

namespace Gr2Port::GameCheck::ArrivalHang {

namespace {

// The arrival's movie, which the story plays between the last scene of Episode 4 and the first
// mission in Jirga Para Lhao, and again in two later missions there. The title screen's own movie
// stays open while the title screen and its menus show.
constexpr std::string_view ArrivalMovie = "mp4/eps05_00100m.mp4";
constexpr std::string_view TitleMovie = "mp4/hud/hud_title_menu.mp4";

// A load into Jirga Para Lhao draws its first world after about 5 seconds.
constexpr auto WorldWait = std::chrono::minutes{1};

// The world frames that end the wait. The frame graph also runs for a few frames at boot.
constexpr u32 WorldFrames = 60;

std::mutex g_mutex;
std::condition_variable_any g_drawn;
const void* g_title = nullptr;
const void* g_arrival = nullptr;
// Whether the title screen's movie was open while the arrival's played.
bool g_at_title = false;
std::atomic<bool> g_waiting = false;
u32 g_world_frames = 0;
// The last member: it ends and joins before the others go.
std::jthread g_watch;

void Watch(const std::stop_token& stop) {
    std::unique_lock lock{g_mutex};
    if (g_drawn.wait_for(lock, stop, WorldWait, [] { return !g_waiting.load(); }) ||
        stop.stop_requested()) {
        return;
    }
    g_waiting = false;
    LOG_ERROR(Loader, "GR2port: no world drawn in the minute after {} closed: the dump is bad",
              ArrivalMovie);
    SDL_Event event{};
    event.type = SDL_EVENT_BAD_DUMP;
    SDL_PushEvent(&event);
}

} // namespace

void OnMovieOpened(const void* player, std::string_view path) {
    std::scoped_lock lock{g_mutex};
    if (path == TitleMovie) {
        g_title = player;
        g_at_title = true;
    } else if (path == ArrivalMovie) {
        g_arrival = player;
        g_at_title = g_title != nullptr;
    }
}

void OnMovieClosed(const void* player) {
    std::unique_lock lock{g_mutex};
    if (player == g_title) {
        g_title = nullptr;
    }
    if (player != g_arrival) {
        return;
    }
    g_arrival = nullptr;
    if (g_at_title) {
        return;
    }
    g_waiting = true;
    g_world_frames = 0;
    lock.unlock();
    g_watch = std::jthread{Watch};
}

void OnWorldFrame() {
    if (!g_waiting.load(std::memory_order_relaxed)) {
        return;
    }
    std::scoped_lock lock{g_mutex};
    if (++g_world_frames == WorldFrames) {
        g_waiting = false;
        g_drawn.notify_all();
    }
}

void Tell(SDL_Window* window) {
    const std::string text = Language::Text("Bad game files detected. Please re-dump your game.");
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Gravity Rush 2", text.c_str(), window);
}

} // namespace Gr2Port::GameCheck::ArrivalHang
