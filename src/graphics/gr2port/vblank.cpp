// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <cmath>

#include "graphics/gr2port/vblank.h"

namespace Gr2Port::Vblank {

namespace {

// How far the monitor's rate may be from the frame rate's, as a share of it, for the vblank to
// follow the monitor. The game then runs that much slower or faster.
constexpr double Tolerance = 0.01;

std::atomic<double> g_refresh_rate{0.0};
std::atomic<bool> g_synced{false};

} // namespace

void SetRefreshRate(double rate) {
    g_refresh_rate.store(rate, std::memory_order_relaxed);
}

void SetSynced(bool synced) {
    g_synced.store(synced, std::memory_order_relaxed);
}

std::chrono::nanoseconds Period(u32 frame_rate) {
    double rate = frame_rate;
    const double refresh = g_refresh_rate.load(std::memory_order_relaxed);
    if (g_synced.load(std::memory_order_relaxed) && refresh > 0.0) {
        // The monitor shows a frame for a whole number of refreshes, or a whole number of frames
        // fall in one refresh.
        const double followed = refresh >= rate ? refresh / std::round(refresh / rate)
                                                : refresh * std::round(rate / refresh);
        if (std::abs(followed - rate) <= rate * Tolerance) {
            rate = followed;
        }
    }
    return std::chrono::nanoseconds{std::llround(1e9 / rate)};
}

} // namespace Gr2Port::Vblank
