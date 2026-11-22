// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "common/types.h"

// The audio backend, and the output devices that the game's sound and the DS4 speaker's sound play
// on for a backend, by the names that the backend opens them by.
namespace Gr2Port::Launcher::AudioDevices {

/// The name that opens the system's default output device.
inline constexpr std::string_view Default = "Default Device";

/// The backends' names, in the order of AudioBackend.
inline constexpr std::array<std::string_view, 2> Backends{"SDL", "OpenAL"};

struct Chosen {
    std::string main{Default};
    std::string pad_speaker{Default};
};

/// The backend the game opens: any setting but OpenAL's is SDL.
u32 Backend();

Chosen Get(u32 backend);
void Set(u32 backend, const Chosen& chosen);

/// Default, the backend's output devices, then any chosen device that is not plugged in, so that
/// saving keeps it.
std::vector<std::string> List(u32 backend, const Chosen& chosen);

} // namespace Gr2Port::Launcher::AudioDevices
