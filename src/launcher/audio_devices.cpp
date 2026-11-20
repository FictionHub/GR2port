// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>

#include "core/emulator_settings.h"
#include "core/libraries/audio/openal_manager.h"
#include "launcher/audio_devices.h"

namespace Gr2Port::Launcher::AudioDevices {

namespace {

bool OpenAL() {
    return EmulatorSettings.GetAudioBackend() == AudioBackend::OpenAL;
}

std::vector<std::string> SdlDevices() {
    std::vector<std::string> names;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        return names;
    }
    int count = 0;
    if (SDL_AudioDeviceID* const devices = SDL_GetAudioPlaybackDevices(&count)) {
        for (int i = 0; i < count; ++i) {
            if (const char* const name = SDL_GetAudioDeviceName(devices[i])) {
                names.emplace_back(name);
            }
        }
        SDL_free(devices);
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return names;
}

} // namespace

Chosen Get() {
    if (OpenAL()) {
        return {EmulatorSettings.GetOpenALMainOutputDevice(),
                EmulatorSettings.GetOpenALPadSpkOutputDevice()};
    }
    return {EmulatorSettings.GetSDLMainOutputDevice(), EmulatorSettings.GetSDLPadSpkOutputDevice()};
}

void Set(const Chosen& chosen) {
    if (OpenAL()) {
        EmulatorSettings.SetOpenALMainOutputDevice(chosen.main);
        EmulatorSettings.SetOpenALPadSpkOutputDevice(chosen.pad_speaker);
    } else {
        EmulatorSettings.SetSDLMainOutputDevice(chosen.main);
        EmulatorSettings.SetSDLPadSpkOutputDevice(chosen.pad_speaker);
    }
}

std::vector<std::string> List(const Chosen& chosen) {
    std::vector<std::string> names{std::string{Default}};
    const std::vector<std::string> devices =
        OpenAL() ? Libraries::AudioOut::OpenALDevice::GetAvailableDevices() : SdlDevices();
    names.insert(names.end(), devices.begin(), devices.end());
    for (const std::string& name : {chosen.main, chosen.pad_speaker}) {
        if (std::ranges::find(names, name) == names.end()) {
            names.push_back(name);
        }
    }
    return names;
}

} // namespace Gr2Port::Launcher::AudioDevices
