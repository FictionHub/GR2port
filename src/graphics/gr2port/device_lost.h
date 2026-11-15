// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <filesystem>
#include <source_location>
#include <string_view>

#include "graphics/gr2port/device.h"

namespace Gr2Port {
class Memory;
}

namespace Gr2Port::Scene {
struct Header;
}

// The log of a lost device: the call that reported it, the frames in flight, the memory, the
// driver's checkpoints and fault, the buffer each fault address lies in, and every device address
// of the frames in flight checked against the buffers. With the trace on, the GPU also writes
// each pass it begins into memory the CPU reads, on every GPU. The log of each run after a loss
// starts with that report, and those runs trace.
namespace Gr2Port::DeviceLost {

enum class Pass : u32 {
    FrameStart,
    Deformers,
    Kernels,
    Simulation,
    PointLights,
    MeshSampling,
    SelfTranslucent,
    Shadows,
    FogVolumes,
    Surfaces,
    Lines,
    CharacterDepth,
    Occlusion,
    Velocity,
    Light,
    Characters,
    Sky,
    Contours,
    EffectsBehind,
    Glass,
    Water,
    EffectsInFront,
    FogApply,
    Particles,
    EffectsTopMost,
    Meter,
    Bloom,
    Antialias,
    Composite,
    Ui,
    Present,
    Count,
};

/// Before the renderer starts: turns the trace on for this run. The log names reason.
void Arm(std::string_view reason);

const char* Name(Pass pass);

/// The number of the frame recorded last.
u64 Frame();

/// Any thread, with the trace on: the last frame that began each pass, by its low half. False
/// without the trace.
bool Crumbs(std::array<u32, static_cast<u32>(Pass::Count)>& crumbs);

/// Before the renderer starts: reads the log at path, the last run's, and keeps the report of a
/// lost device it holds for this run's log: that run's own, or the one it kept from a run before
/// it. True when it holds one.
bool KeepLastReport(const std::filesystem::path& path);

/// Render thread, once: keeps memory for the report and, with the trace on, makes the buffer the
/// GPU writes its passes into. frames_in_flight frames keep their packets until they are done.
void Start(const Device& device, Memory& memory, u32 frames_in_flight);

/// Any thread: a buffer the GPU reads by address exists from here on, or is freed.
void AddBuffer(u64 address, u64 size, const std::source_location& where);
void RemoveBuffer(u64 address);

/// Outside a rendering: records that the GPU reached the start of pass.
void Mark(const Device& device, vk::CommandBuffer cmd, Pass pass);

/// As Mark, inside a rendering whose attachments load: with the trace on, the rendering ends
/// before the mark and begins again after it.
void Mark(const Device& device, vk::CommandBuffer cmd, Pass pass,
          const vk::RenderingInfo& rendering);

/// Inside a rendering whose attachments load, with the trace on: records that the GPU reached
/// draw index of the pass it is in. The rendering ends before the mark and begins again after it.
void MarkDraw(const Device& device, vk::CommandBuffer cmd, u32 index,
              const vk::RenderingInfo& rendering);

/// Any thread, with the trace on: the draw MarkDraw recorded last, and the low half of its frame.
/// False without the trace.
bool LastDraw(u32& frame, u32& index);

/// Render thread, after the frame's first mark: the frame draws packet, which starts with header.
void Note(const Scene::Header& header, const u8* packet);

/// Render thread: the frame went to the GPU.
void Submitted();

/// Any thread, while the render thread waits for the GPU: logs the frames in flight as a lost
/// device's report does, their records' addresses checked, and the records the game's own
/// programs draw from as words.
void ReportStall();

/// Given the result of call: when it says the device is lost, logs the report, once.
void Check(const Device& device, vk::Result result, std::string_view call);

} // namespace Gr2Port::DeviceLost
