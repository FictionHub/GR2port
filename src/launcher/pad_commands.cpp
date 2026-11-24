// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <vector>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_init.h>

#include "launcher/pad_commands.h"

namespace Gr2Port::Launcher::PadCommands {

namespace {

using Clock = std::chrono::steady_clock;

// A held direction repeats after RepeatDelay, then every RepeatInterval.
constexpr auto RepeatDelay = std::chrono::milliseconds{400};
constexpr auto RepeatInterval = std::chrono::milliseconds{100};
// The left stick points once an axis passes half its travel, and keeps pointing until the axis
// falls back under a quarter of it, so that a stick held near the threshold gives one command.
constexpr int StickPress = 16384;
constexpr int StickRelease = 8192;
// Commands the windows have not taken yet. Beyond this many, the oldest is dropped.
constexpr size_t MaxPending = 8;

struct Binding {
    SDL_GamepadButton button;
    Command command;
};

constexpr std::array Buttons{
    Binding{SDL_GAMEPAD_BUTTON_SOUTH, Command::Accept},
    Binding{SDL_GAMEPAD_BUTTON_EAST, Command::Back},
    Binding{SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, Command::PreviousTab},
    Binding{SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, Command::NextTab},
};

constexpr std::array DPad{
    Binding{SDL_GAMEPAD_BUTTON_DPAD_UP, Command::Up},
    Binding{SDL_GAMEPAD_BUTTON_DPAD_DOWN, Command::Down},
    Binding{SDL_GAMEPAD_BUTTON_DPAD_LEFT, Command::Left},
    Binding{SDL_GAMEPAD_BUTTON_DPAD_RIGHT, Command::Right},
};

struct State {
    bool started = false;
    std::vector<SDL_Gamepad*> pads;
    std::array<bool, Buttons.size()> held{};
    std::optional<Command> direction;
    // When the held direction gives its next command: never, while it waits to be let go.
    Clock::time_point repeat_at = Clock::time_point::max();
    int pauses = 0;
    std::deque<Command> pending;
};

State g_state;

// Opens the pads plugged in since the last look, and closes those unplugged.
void FindPads() {
    std::erase_if(g_state.pads, [](SDL_Gamepad* pad) {
        if (SDL_GamepadConnected(pad)) {
            return false;
        }
        SDL_CloseGamepad(pad);
        return true;
    });
    int count = 0;
    SDL_JoystickID* const ids = SDL_GetGamepads(&count);
    if (ids == nullptr) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        const bool open = std::ranges::any_of(
            g_state.pads, [id = ids[i]](SDL_Gamepad* pad) { return SDL_GetGamepadID(pad) == id; });
        if (!open) {
            if (SDL_Gamepad* const pad = SDL_OpenGamepad(ids[i])) {
                g_state.pads.push_back(pad);
            }
        }
    }
    SDL_free(ids);
}

// How far a pad's left stick points along a direction.
int Along(SDL_Gamepad* pad, Command direction) {
    switch (direction) {
    case Command::Up:
        return -SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    case Command::Down:
        return SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    case Command::Left:
        return -SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
    case Command::Right:
        return SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
    default:
        return 0;
    }
}

// The direction the pads point in: a D-pad's, else the one a left stick keeps, else a left
// stick's newly pushed axis.
std::optional<Command> Pointed() {
    for (SDL_Gamepad* const pad : g_state.pads) {
        for (const Binding& binding : DPad) {
            if (SDL_GetGamepadButton(pad, binding.button)) {
                return binding.command;
            }
        }
    }
    if (g_state.direction) {
        for (SDL_Gamepad* const pad : g_state.pads) {
            if (Along(pad, *g_state.direction) > StickRelease) {
                return g_state.direction;
            }
        }
    }
    for (SDL_Gamepad* const pad : g_state.pads) {
        const int x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
        const int y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
        if (std::max(std::abs(x), std::abs(y)) < StickPress) {
            continue;
        }
        if (std::abs(x) > std::abs(y)) {
            return x < 0 ? Command::Left : Command::Right;
        }
        return y < 0 ? Command::Up : Command::Down;
    }
    return std::nullopt;
}

void Give(Command command) {
    if (g_state.pending.size() == MaxPending) {
        g_state.pending.pop_front();
    }
    g_state.pending.push_back(command);
}

// Takes the pads' state. New presses give commands only when give is set; otherwise a held
// direction waits to be let go.
void Look(bool give) {
    SDL_UpdateGamepads();
    FindPads();
    for (size_t i = 0; i < Buttons.size(); ++i) {
        const bool down = std::ranges::any_of(g_state.pads, [&](SDL_Gamepad* pad) {
            return SDL_GetGamepadButton(pad, Buttons[i].button);
        });
        if (down && !g_state.held[i] && give) {
            Give(Buttons[i].command);
        }
        g_state.held[i] = down;
    }
    const Clock::time_point now = Clock::now();
    const std::optional<Command> pointed = Pointed();
    if (pointed != g_state.direction) {
        g_state.direction = pointed;
        g_state.repeat_at = Clock::time_point::max();
        if (pointed && give) {
            Give(*pointed);
            g_state.repeat_at = now + RepeatDelay;
        }
    } else if (!give) {
        g_state.repeat_at = Clock::time_point::max();
    } else if (pointed && now >= g_state.repeat_at) {
        Give(*pointed);
        g_state.repeat_at = now + RepeatInterval;
    }
}

} // namespace

void Start() {
    if (!g_state.started) {
        g_state.started = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    }
}

void Stop() {
    for (SDL_Gamepad* const pad : g_state.pads) {
        SDL_CloseGamepad(pad);
    }
    if (g_state.started) {
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    }
    g_state = State{};
}

std::optional<Command> Next() {
    if (!g_state.started) {
        return std::nullopt;
    }
    Look(g_state.pauses == 0);
    if (g_state.pending.empty()) {
        return std::nullopt;
    }
    const Command command = g_state.pending.front();
    g_state.pending.pop_front();
    return command;
}

Pause::Pause() {
    ++g_state.pauses;
    g_state.pending.clear();
}

Pause::~Pause() {
    if (--g_state.pauses == 0 && g_state.started) {
        Look(false);
    }
}

} // namespace Gr2Port::Launcher::PadCommands
