// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <bit>
#include <xbyak/xbyak.h>

#include "common/logging/log.h"
#include "graphics/gr2port/hooks.h"
#include "graphics/gr2port/motion_ends.h"

namespace Gr2Port::MotionEnds {

namespace {

constexpr VAddr GoldHenBase = 0x400000;

// FUN_008f6430, for each motion of a layer: mov edx, [rcx + 0x3c] loads how many more times the
// motion plays, 0 for once, and add eax, r15d adds the layer's step in ticks to the playhead in
// eax. esi holds the motion's length in ticks, at least 1. The game writes r13d, xmm5 and xmm7
// before it reads them again.
constexpr Hooks::Entry Advance{0x8fb830, 6, {0x8b, 0x51, 0x3c, 0x44, 0x01, 0xf8}};

constexpr float Half = 0.5f;
constexpr float One = 1.0f;

// The stub and its constants.
constexpr size_t StubBytes = 128;

} // namespace

bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity) {
    using namespace Xbyak::util;
    const auto code = [module_base](VAddr address) {
        return reinterpret_cast<const u8*>(module_base + address - GoldHenBase);
    };
    if (capacity - c.getSize() < StubBytes) {
        LOG_ERROR(Render_Vulkan, "GR2port: no room for the hook of the motions' ends");
        return false;
    }
    Xbyak::Label game_step, half, one;
    const void* advance = c.getCurr();
    const u8* next = code(Advance.address + Advance.size);
    c.mov(edx, dword[rcx + 0x3c]);
    c.test(edx, edx);
    c.jne(game_step);
    c.test(r15d, r15d);
    c.jle(game_step);
    c.mov(r13d, esi);
    c.sub(r13d, eax);
    c.jle(game_step);
    // The steps left at about the game step, at least 1, and the rest of the motion split into
    // that many.
    c.vcvtsi2ss(xmm5, xmm5, r13d);
    c.vcvtsi2ss(xmm7, xmm7, r15d);
    c.vdivss(xmm7, xmm5, xmm7);
    c.vaddss(xmm7, xmm7, dword[rip + half]);
    c.vroundss(xmm7, xmm7, xmm7, 1);
    c.vmaxss(xmm7, xmm7, dword[rip + one]);
    c.vdivss(xmm5, xmm5, xmm7);
    c.vaddss(xmm5, xmm5, dword[rip + half]);
    c.vcvttss2si(r13d, xmm5);
    c.add(eax, r13d);
    c.jmp(next);
    c.L(game_step);
    c.add(eax, r15d);
    c.jmp(next);
    c.L(half);
    c.dd(std::bit_cast<u32>(Half));
    c.L(one);
    c.dd(std::bit_cast<u32>(One));
    if (Hooks::Replace(module_base, Advance, c, capacity, advance) != Hooks::Result::Hooked) {
        LOG_ERROR(Render_Vulkan, "GR2port: the motions' advance is not the expected code");
        return false;
    }
    return true;
}

} // namespace Gr2Port::MotionEnds
