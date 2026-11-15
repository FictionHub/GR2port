// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>
#include <fmt/format.h>

#include "common/debug_files.h"
#include "common/logging/log.h"
#include "common/path_util.h"
#include "graphics/gr2port/catcher.h"
#include "graphics/gr2port/pass_times.h"

namespace Gr2Port::PassTimes {

namespace {

constexpr u32 Passes = static_cast<u32>(Pass::Count);
// The first mark, then one per pass.
constexpr u32 Marks = Passes + 1;

constexpr std::array<const char*, Passes> Names{
    "uploads",
    "deformers",
    "kernels",
    "sim copies",
    "sim update",
    "sim clear",
    "sim move",
    "sim spawn",
    "sim samples",
    "point lights",
    "self translucent",
    "shadow opaque 0",
    "shadow alpha 0",
    "shadow leaves 0",
    "shadow particles 0",
    "shadow opaque 1",
    "shadow alpha 1",
    "shadow leaves 1",
    "shadow particles 1",
    "shadow opaque 2",
    "shadow alpha 2",
    "shadow leaves 2",
    "shadow particles 2",
    "fog bake",
    "fog density",
    "fog shadow",
    "fog scatter",
    "world opaque",
    "world decals",
    "metaballs",
    "vegetation",
    "particle surfaces",
    "surfaces end",
    "lines",
    "character depth",
    "occlusion",
    "velocity",
    "light",
    "lambert",
    "enemies forward",
    "characters",
    "sky",
    "enemies alpha",
    "character glass",
    "contours",
    "low opaque",
    "effects bg",
    "low transparent",
    "glass",
    "water",
    "effects fg",
    "fog apply",
    "particles",
    "effects topmost",
    "meter",
    "bloom",
    "motion blur",
    "antialias",
    "composite",
    "ui",
    "present",
};

struct Counts {
    u64 frame;
    u32 draws;
    u32 particle_draws;
    u32 effect_draws;
    u32 enemies;
    u32 metaballs;
};

// The slots Poll can be asked about.
constexpr u32 MaxSlots = 4;

const Device* g_device = nullptr;
vk::QueryPool g_pool;
u32 g_slot = 0;
// Per slot, for Poll: a bit for each mark the frame's commands hold, and the game's frame.
std::array<std::atomic<u64>, MaxSlots> g_written{};
std::array<std::atomic<u64>, MaxSlots> g_game_frame{};
// Per slot, the marks' values of the frame before the one its commands hold. The GPU clears a
// slot's marks only when it begins the new frame, so until then they read as that frame's: a
// mark is the new frame's once its value differs.
std::array<std::array<std::atomic<u64>, Marks>, MaxSlots> g_before{};
std::vector<Counts> g_counts;
std::vector<bool> g_pending;
std::FILE* g_file = nullptr;
std::chrono::steady_clock::time_point g_start;

u32 g_frames = 0;
std::array<double, Passes + 1> g_sum{};
std::array<double, Passes + 1> g_min{};
std::array<double, Passes + 1> g_max{};

void Clear() {
    g_frames = 0;
    g_sum.fill(0.0);
    g_min.fill(std::numeric_limits<double>::max());
    g_max.fill(0.0);
}

} // namespace

const char* Name(Pass pass) {
    return Names[static_cast<u32>(pass)];
}

Progress Poll(u32 slot) {
    Progress progress{};
    if (!g_pool || slot >= std::min<size_t>(g_counts.size(), MaxSlots)) {
        return progress;
    }
    const u64 written = g_written[slot].load(std::memory_order_relaxed);
    progress.game_frame = g_game_frame[slot].load(std::memory_order_relaxed);
    std::array<std::array<u64, 2>, Marks> marks{};
    const vk::Result result = g_device->device.getQueryPoolResults(
        g_pool, Marks * slot, Marks, sizeof(marks), marks.data(), sizeof(marks[0]),
        vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWithAvailability,
        g_device->dispatch);
    if (result != vk::Result::eSuccess && result != vk::Result::eNotReady) {
        return progress;
    }
    progress.known = true;
    u32 passed = Marks;
    const auto fresh = [&](u32 mark) {
        return marks[mark][1] != 0 &&
               marks[mark][0] != g_before[slot][mark].load(std::memory_order_relaxed);
    };
    for (u32 mark = 0; mark < Marks; mark++) {
        if ((written >> mark & 1) == 0) {
            continue;
        }
        progress.written++;
        if (fresh(mark)) {
            progress.finished++;
            passed = mark;
        } else if (progress.next == Passes && mark != 0 && (passed == Marks || mark > passed)) {
            progress.next = mark - 1;
        }
    }
    if (passed != Marks && passed != 0) {
        progress.last = passed - 1;
    }
    progress.done = (written >> (Marks - 1) & 1) != 0 && fresh(Marks - 1);
    return progress;
}

void Start(const Device& device, u32 slots) {
    g_device = &device;
    if (device.timestamp_period == 0.0) {
        return;
    }
    g_pool =
        device.device
            .createQueryPool({.queryType = vk::QueryType::eTimestamp, .queryCount = Marks * slots},
                             nullptr, device.dispatch)
            .value;
    g_counts.resize(slots);
    g_pending.resize(slots);
    Clear();
    g_start = std::chrono::steady_clock::now();
    if (!Common::DebugFiles()) {
        return;
    }
    const auto path =
        Common::FS::GetUserPath(Common::FS::PathType::LogDir) / "gr2port_pass_times.csv";
    g_file = std::fopen(path.string().c_str(), "w");
    if (g_file != nullptr) {
        std::setvbuf(g_file, nullptr, _IOFBF, 1 << 20);
        std::string line =
            "frame,seconds,draws,particle draws,effect draws,enemies,metaballs,total";
        for (const char* name : Names) {
            line += fmt::format(",{}", name);
        }
        line += '\n';
        std::fputs(line.c_str(), g_file);
    }
}

void Begin(vk::CommandBuffer cmd, u32 slot, const Scene::Header& header) {
    if (!g_pool) {
        return;
    }
    g_slot = slot;
    g_counts[slot] = {
        .frame = header.frame,
        .draws = header.draws,
        .particle_draws = header.particles.draws,
        .effect_draws = header.effects.draws,
        .enemies = header.enemies,
        .metaballs = header.metaballs,
    };
    g_pending[slot] = true;
    if (slot < MaxSlots) {
        g_written[slot].store(1, std::memory_order_relaxed);
        g_game_frame[slot].store(header.frame, std::memory_order_relaxed);
    }
    cmd.resetQueryPool(g_pool, Marks * slot, Marks, g_device->dispatch);
    cmd.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, g_pool, Marks * slot,
                        g_device->dispatch);
}

void Mark(vk::CommandBuffer cmd, Pass pass) {
    if (!g_pool) {
        return;
    }
    if (g_slot < MaxSlots) {
        g_written[g_slot].fetch_or(u64{2} << static_cast<u32>(pass), std::memory_order_relaxed);
    }
    cmd.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, g_pool,
                        Marks * g_slot + 1 + static_cast<u32>(pass), g_device->dispatch);
}

void Collect(u32 slot) {
    if (!g_pool || !g_pending[slot]) {
        return;
    }
    g_pending[slot] = false;
    // Each mark's value and its availability; a pass whose mark was not written takes no time.
    std::array<std::array<u64, 2>, Marks> marks{};
    const vk::Result result = g_device->device.getQueryPoolResults(
        g_pool, Marks * slot, Marks, sizeof(marks), marks.data(), sizeof(marks[0]),
        vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWithAvailability,
        g_device->dispatch);
    if (result != vk::Result::eSuccess && result != vk::Result::eNotReady) {
        return;
    }
    if (slot < MaxSlots) {
        for (u32 mark = 0; mark < Marks; mark++) {
            if (marks[mark][1] != 0) {
                g_before[slot][mark].store(marks[mark][0], std::memory_order_relaxed);
            }
        }
    }
    if (marks[0][1] == 0) {
        return;
    }
    const double ms = g_device->timestamp_period * 1e-6;
    std::array<double, Passes + 1> times{};
    u64 last = marks[0][0];
    for (u32 p = 0; p < Passes; p++) {
        if (marks[p + 1][1] == 0 || marks[p + 1][0] < last) {
            continue;
        }
        times[p + 1] = static_cast<double>(marks[p + 1][0] - last) * ms;
        last = marks[p + 1][0];
    }
    times[0] = static_cast<double>(last - marks[0][0]) * ms;
    for (u32 i = 0; i <= Passes; i++) {
        g_sum[i] += times[i];
        g_min[i] = std::min(g_min[i], times[i]);
        g_max[i] = std::max(g_max[i], times[i]);
    }
    g_frames++;
    if (Catcher::On) {
        Catcher::FrameTimes(g_counts[slot].frame, times);
    }
    if (g_file != nullptr) {
        const Counts& c = g_counts[slot];
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
        std::string line = fmt::format("{},{:.3f},{},{},{},{},{}", c.frame, seconds, c.draws,
                                       c.particle_draws, c.effect_draws, c.enemies, c.metaballs);
        for (const double t : times) {
            line += fmt::format(",{:.4f}", t);
        }
        line += '\n';
        std::fputs(line.c_str(), g_file);
    }
}

void Report() {
    if (!g_pool || g_frames == 0) {
        return;
    }
    std::string line =
        fmt::format("{} frames, ms mean/shortest/longest: total {:.3f}/{:.3f}/{:.3f}", g_frames,
                    g_sum[0] / g_frames, g_min[0], g_max[0]);
    for (u32 p = 0; p < Passes; p++) {
        line += fmt::format(", {} {:.3f}/{:.3f}/{:.3f}", Names[p], g_sum[p + 1] / g_frames,
                            g_min[p + 1], g_max[p + 1]);
    }
    LOG_INFO(Render_Vulkan, "GR2port passes: {}", line);
    if (g_file != nullptr) {
        std::fflush(g_file);
    }
    Clear();
}

} // namespace Gr2Port::PassTimes
