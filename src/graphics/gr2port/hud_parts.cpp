// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>

#include "common/logging/log.h"
#include "graphics/gr2port/hooks.h"
#include "graphics/gr2port/hud_parts.h"

namespace Gr2Port::HudParts {

namespace {

// The drawing functions of the HUD objects, slot 9 of their virtual tables.
constexpr Hooks::Entry GravityMeter{0x16f92f0, 6, Hooks::FramePrologueR14};
constexpr Hooks::Entry SpecialMeter{0x16fe470, 6, Hooks::FramePrologue};
constexpr Hooks::Entry HealthBar{0x16fb4f0, 6, Hooks::FramePrologue};
constexpr Hooks::Entry Crosshair{0x1741a80, 6, Hooks::FramePrologue};

std::atomic<VAddr> g_base{0};

void Set(VAddr base, const Hooks::Entry& entry, bool shown) {
    if (Hooks::SetReturning(base, entry, !shown) != Hooks::Result::Hooked) {
        LOG_ERROR(Render_Vulkan, "GR2port: the HUD drawing at {:#x} is not the expected code",
                  entry.address);
    }
}

} // namespace

void Install(VAddr module_base) {
    g_base.store(module_base, std::memory_order_relaxed);
    Choose(Settings::Get().graphics);
}

void Choose(const Settings::Graphics& graphics) {
    const VAddr base = g_base.load(std::memory_order_relaxed);
    if (base == 0) {
        return;
    }
    Set(base, GravityMeter, graphics.hud_gravity_meter);
    Set(base, SpecialMeter, graphics.hud_gravity_meter);
    Set(base, HealthBar, graphics.hud_health_bar);
    Set(base, Crosshair, graphics.hud_crosshair);
}

} // namespace Gr2Port::HudParts
