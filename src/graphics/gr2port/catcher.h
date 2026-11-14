// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include <vector>

#include "graphics/gr2port/device.h"

namespace Gr2Port {
class Memory;
}

namespace Gr2Port::Scene {
struct Header;
}

// A build for the players whose GPU device is lost. At start it logs the Vulkan loader's layers,
// every GPU, the picked GPU's features, limits, memory and extensions, the process's modules, the
// displays and the environment. It takes the driver's own messages into the log. While the game
// runs it keeps the last frames' CPU and GPU times and watches the GPU: when a frame is not done
// in time it logs the pass the GPU is in, before the driver gives the device up. A lost device's
// report ends with all of that.
namespace Gr2Port::Catcher {

inline constexpr bool On = true;

/// What the render thread does.
enum class Call : u32 {
    Idle,
    Wait,
    Acquire,
    Record,
    Submit,
    Present,
    Count,
};

/// Before the instance: adds the extension the driver's messages come through, when the loader
/// has it.
void ExtendInstance(std::vector<const char*>& extensions,
                    const vk::detail::DispatchLoaderDynamic& dispatch);

/// With the instance: logs the loader, the layers, the extensions and every GPU, and takes the
/// driver's messages from here on.
void LogInstance(const Device& device);

/// With the device: logs the picked GPU in full, and the system around the process.
void LogDevice(const Device& device);

/// Render thread, once: starts the thread that watches the GPU, the render thread and the game.
void Start(Memory& memory);

/// Render thread: a frame begins; the time since the last one ended was spent waiting for it.
void BeginFrame(const Scene::Header& header);

/// Render thread: what it does from here on.
void Enter(Call call);

/// Render thread: the frame ends; submitted says whether it went to the GPU, from slot.
void EndFrame(bool submitted, u32 slot);

/// Render thread: the GPU times of the frame that drew game_frame, the total first.
void FrameTimes(u64 game_frame, std::span<const double> ms);

/// Render thread, in a lost device's report: the watch's last lines, the GPU's progress, the last
/// frames and what changed in the process since the start.
void ReportLoss(const Device& device);

} // namespace Gr2Port::Catcher
