// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "graphics/gr2port/device.h"
#include "graphics/gr2port/scene.h"

// GPU time of each pass: a timestamp after each, a line per report and a row per frame in
// gr2port_pass_times.csv in the log folder. Without Start, the other functions do nothing.
namespace Gr2Port::PassTimes {

/// Each pass ends at its mark, in recording order.
enum class Pass : u32 {
    Uploads,
    Deformers,
    Kernels,
    SimCopies,
    SimUpdate,
    SimClear,
    SimMove,
    SimSpawn,
    Simulation,
    PointLights,
    SelfTranslucent,
    ShadowOpaque0,
    ShadowAlpha0,
    ShadowLeaves0,
    ShadowParticles0,
    ShadowOpaque1,
    ShadowAlpha1,
    ShadowLeaves1,
    ShadowParticles1,
    ShadowOpaque2,
    ShadowAlpha2,
    ShadowLeaves2,
    Shadows,
    FogBake,
    FogDensity,
    FogShadow,
    FogVolumes,
    WorldOpaque,
    WorldDecals,
    Metaballs,
    Vegetation,
    ParticleSurfaces,
    SurfacesEnd,
    Lines,
    CharacterDepth,
    Occlusion,
    Velocity,
    Light,
    Lambert,
    EnemiesForward,
    Characters,
    Sky,
    EnemiesAlpha,
    CharacterGlass,
    Contours,
    LowOpaque,
    EffectsBg,
    LowTransparent,
    Glass,
    Water,
    EffectsFg,
    FogApply,
    Particles,
    EffectsTopMost,
    Meter,
    Bloom,
    MotionBlur,
    Antialias,
    Composite,
    Ui,
    Present,
    Count,
};

/// The GPU's progress with the frame in a slot: whether its marks could be read, whether the
/// frame is done, the marks its commands hold and those the GPU has passed, the pass whose mark it
/// passed last and the pass it is in, each Pass::Count when there is none.
struct Progress {
    bool known = false;
    bool done = false;
    u64 game_frame = 0;
    u32 written = 0;
    u32 finished = 0;
    u32 last = static_cast<u32>(Pass::Count);
    u32 next = static_cast<u32>(Pass::Count);
};

const char* Name(Pass pass);

/// Creates the query pool for slots frame slots and, with --debug-files, opens the file of every
/// frame's times.
void Start(const Device& device, u32 slots);

/// Any thread, without waiting for the GPU: its progress with the frame in slot.
Progress Poll(u32 slot);

/// At the start of a frame's commands in slot: resets its marks and writes the first.
void Begin(vk::CommandBuffer cmd, u32 slot, const Scene::Header& header);

/// After pass, once per frame.
void Mark(vk::CommandBuffer cmd, Pass pass);

/// With slot's fence signalled: adds its frame's times.
void Collect(u32 slot);

/// Logs the means, shortest and longest since the last report.
void Report();

} // namespace Gr2Port::PassTimes
