// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include <queue>

#include "common/types.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <Windows.h>
using ThreadID = DWORD;
#else
#include <pthread.h>
#include <signal.h>
using ThreadID = pthread_t;
#endif

namespace Core::Devtools {
class Layer;
namespace Widget {
class FrameGraph;
} // namespace Widget
} // namespace Core::Devtools

namespace DebugStateType {

extern bool showing_debug_menu_bar;

class DebugStateImpl {
    friend class Core::Devtools::Layer;
    friend class Core::Devtools::Widget::FrameGraph;

    std::queue<std::string> debug_message_popup;

    std::mutex guest_threads_mutex{};
    std::vector<ThreadID> guest_threads{};
    std::atomic_bool is_guest_threads_paused = false;
    u64 pause_time{};

    std::atomic_int32_t flip_frame_count = 0;

public:
    float Framerate = 1.0f / 60.0f;
    float FrameDeltaTime;

    std::pair<u32, u32> game_resolution{};
    std::pair<u32, u32> output_resolution{};
    bool is_using_fsr{};

    void ShowDebugMessage(std::string message) {
        if (message.empty()) {
            return;
        }
        debug_message_popup.push(std::move(message));
    }

    bool& IsShowingDebugMenuBar() {
        return showing_debug_menu_bar;
    }

    void AddCurrentThreadToGuestList();

    void RemoveCurrentThreadFromGuestList();

    void PauseGuestThreads();

    void ResumeGuestThreads();

    bool IsGuestThreadsPaused() const {
        return is_guest_threads_paused;
    }

    void IncFlipFrameNum() {
        ++flip_frame_count;
    }

    u32 GetFrameNum() const {
        return flip_frame_count;
    }
};
} // namespace DebugStateType

extern DebugStateType::DebugStateImpl& DebugState;
