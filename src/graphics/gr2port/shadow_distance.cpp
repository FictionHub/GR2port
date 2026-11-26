// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <cstddef>

#include <xbyak/xbyak.h>

#include "common/logging/log.h"
#include "graphics/gr2port/hooks.h"
#include "graphics/gr2port/shadow_distance.h"

namespace Gr2Port::ShadowDistance {

namespace {

// FUN_01421f60, with the scene's parameters in rax: mov ecx, [rax + 0x50]; mov [0x1c81fdc], ecx
// copies where the far shadow map ends. The code after it stores the end times [rax + 0x58] at
// 0x1c81b58, copies [rax + 0x54] to 0x1c81fd8 and the mode 2 casting distance [rax + 0x5c] to
// 0x1c81fd0, and leaves ecx and eax holding the last two.
constexpr Hooks::Entry Copy{0x1422033, 9, {0x8b, 0x48, 0x50, 0x89, 0x0d, 0xa0, 0xff, 0x85, 0x00}};
constexpr VAddr CopyNext = 0x142205f;
constexpr VAddr EndTimesShare = 0x1c81b58;
constexpr u32 WordAt = 0x1c81fd8 - EndTimesShare;
constexpr u32 EndAt = 0x1c81fdc - EndTimesShare;
constexpr u32 CastingAt = 0x1c81fd0 - EndTimesShare;

// FUN_014de070: vucomiss xmm1, [0x18c5680] compares a model's squared distance from the eye with
// 600 m squared, past which it casts no shadow.
constexpr Hooks::Entry Farthest{0x14de151, 8, {0xc5, 0xf8, 0x2e, 0x0d, 0x27, 0x75, 0x3e, 0x00}};
constexpr VAddr FarthestNext = 0x14de159;
constexpr float FarthestSquared = 360000.0f;

// FUN_014de070, for a model of casting mode 0: vmulss xmm0, xmm0, [0x18c5684] multiplies its
// squared radius by 20 squared, the squared distance past which it casts no shadow.
constexpr Hooks::Entry Radius{0x14de166, 8, {0xc5, 0xfa, 0x59, 0x05, 0x16, 0x75, 0x3e, 0x00}};
constexpr VAddr RadiusNext = 0x14de16e;
constexpr float RadiusSquared = 400.0f;

// The stubs and their values take about 120 bytes.
constexpr size_t StubBytes = 192;

// What the stubs read: the casting distances' factor and the two squared casting limits.
struct Values {
    float casting;
    float farthest;
    float radius;
};
Values* g_values = nullptr;

// The share of the casting distances a shadow quality keeps. Off draws no shadows, so the game
// may as well submit fewer casters.
float CastingShare(Settings::Quality quality) {
    switch (quality) {
    case Settings::Quality::Medium:
        return 0.75f;
    case Settings::Quality::Low:
    case Settings::Quality::Off:
        return 0.5f;
    default:
        return 1.0f;
    }
}

} // namespace

bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity) {
    using namespace Xbyak::util;
    if (capacity - c.getSize() < StubBytes) {
        LOG_ERROR(Render_Vulkan, "GR2port: no room for the shadow distance's hooks");
        return false;
    }
    c.align(16);
    Xbyak::Label values;
    c.L(values);
    g_values = reinterpret_cast<Values*>(const_cast<u8*>(c.getCurr()));
    for (size_t i = 0; i < sizeof(Values) / 4; i++) {
        c.dd(0);
    }
    SetQuality(Settings::Get().graphics.shadows);

    const void* copy = c.getCurr();
    c.mov(rcx, module_base + EndTimesShare - Hooks::CodeBase);
    c.vmovss(xmm1, dword[rax + 0x54]);
    c.vmovss(dword[rcx + WordAt], xmm1);
    c.vmovss(xmm1, dword[rax + 0x50]);
    c.vmovss(dword[rcx + EndAt], xmm1);
    c.vmulss(xmm1, xmm1, dword[rax + 0x58]);
    c.vmovss(dword[rcx], xmm1);
    c.vmovss(xmm1, dword[rax + 0x5c]);
    c.vmulss(xmm1, xmm1, dword[rip + values + static_cast<int>(offsetof(Values, casting))]);
    c.vmovss(dword[rcx + CastingAt], xmm1);
    c.mov(ecx, dword[rax + 0x54]);
    c.mov(eax, dword[rax + 0x5c]);
    c.jmp(Hooks::Code(module_base, CopyNext));

    const void* farthest = c.getCurr();
    c.vucomiss(xmm1, dword[rip + values + static_cast<int>(offsetof(Values, farthest))]);
    c.jmp(Hooks::Code(module_base, FarthestNext));

    const void* radius = c.getCurr();
    c.vmulss(xmm0, xmm0, dword[rip + values + static_cast<int>(offsetof(Values, radius))]);
    c.jmp(Hooks::Code(module_base, RadiusNext));

    if (!Hooks::Holds(module_base, Copy) || !Hooks::Holds(module_base, Farthest) ||
        !Hooks::Holds(module_base, Radius) ||
        Hooks::Replace(module_base, Copy, c, capacity, copy) != Hooks::Result::Hooked ||
        Hooks::Replace(module_base, Farthest, c, capacity, farthest) != Hooks::Result::Hooked ||
        Hooks::Replace(module_base, Radius, c, capacity, radius) != Hooks::Result::Hooked) {
        LOG_ERROR(Render_Vulkan, "GR2port: the shadow distances are not the expected code");
        return false;
    }
    return true;
}

void SetQuality(Settings::Quality quality) {
    if (g_values == nullptr) {
        return;
    }
    const float share = CastingShare(quality);
    std::atomic_ref<float>(g_values->casting).store(share, std::memory_order_relaxed);
    std::atomic_ref<float>(g_values->farthest)
        .store(FarthestSquared * share * share, std::memory_order_relaxed);
    std::atomic_ref<float>(g_values->radius)
        .store(RadiusSquared * share * share, std::memory_order_relaxed);
}

} // namespace Gr2Port::ShadowDistance
