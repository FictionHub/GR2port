// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>
#include <vector>

// The output devices that the game's sound and the DS4 speaker's sound play on, for the audio
// backend in use, by the names that the backend opens them by.
namespace Gr2Port::Launcher::AudioDevices {

/// The name that opens the system's default output device.
inline constexpr std::string_view Default = "Default Device";

struct Chosen {
    std::string main{Default};
    std::string pad_speaker{Default};
};

Chosen Get();
void Set(const Chosen& chosen);

/// Default, the backend's output devices, then any chosen device that is not plugged in, so that
/// saving keeps it.
std::vector<std::string> List(const Chosen& chosen);

} // namespace Gr2Port::Launcher::AudioDevices
