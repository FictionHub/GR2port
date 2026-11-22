// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <bit>
#include <xbyak/xbyak.h>

#include "common/logging/log.h"
#include "graphics/gr2port/hooks.h"
#include "graphics/gr2port/layout_clock.h"

namespace Gr2Port::LayoutClock {

namespace {

// FUN_013e4a90, which steps a layout animation, with the step in ticks in xmm1:
// vaddss xmm1, xmm1, [0.5] rounds it before vcvttss2si eax, xmm1 takes the whole ticks.
constexpr Hooks::Entry StepRounding{0x13e5700, 8, {0xc5, 0xf2, 0x58, 0x0d, 0x54, 0xf6, 0x4d, 0x00}};
constexpr VAddr StepRoundingNext = 0x13e5708;

// The fps patch's running share of a 30 fps frame, from 0 up to 1, a float. It moves on by the
// real frame's time once per game frame.
constexpr VAddr FramePhase = 0x1f7c604;

// The stub and its constants.
constexpr size_t StubBytes = 64;

} // namespace

bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity) {
    using namespace Xbyak::util;
    if (capacity - c.getSize() < StubBytes) {
        LOG_ERROR(Render_Vulkan, "GR2port: no room for the layout clock's hook");
        return false;
    }
    const auto code = [module_base](VAddr address) { return Hooks::Code(module_base, address); };
    Xbyak::Label whole_tick, one, half;

    // The fps patch scales the speed of a comic's caption layout on top of its step, which leaves
    // it under one tick a frame at 360 fps. A step that short keeps the game's rounding.
    const void* stub = c.getCurr();
    c.vcomiss(xmm1, dword[rip + one]);
    c.jae(whole_tick);
    c.vaddss(xmm1, xmm1, dword[rip + half]);
    c.jmp(code(StepRoundingNext));
    c.L(whole_tick);
    c.vaddss(xmm1, xmm1, dword[rip + code(FramePhase)]);
    c.jmp(code(StepRoundingNext));
    c.L(one);
    c.dd(std::bit_cast<u32>(1.0f));
    c.L(half);
    c.dd(std::bit_cast<u32>(0.5f));
    if (Hooks::Replace(module_base, StepRounding, c, capacity, stub) != Hooks::Result::Hooked) {
        LOG_ERROR(Render_Vulkan, "GR2port: the layout clock's rounding is not the expected code");
        return false;
    }
    return true;
}

} // namespace Gr2Port::LayoutClock
