// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <bit>
#include <cstring>
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

// bool FUN_013cdf30(float blend, player, u32 layer, u32 motion, s32 repeats, u32 flags): plays a
// motion on a layer. The motion fades in over blend seconds of motion time while the layer's last
// motion fades out over the same time, and a blend of 0 replaces that motion at once.
constexpr Hooks::Entry Play{0x13cdf30, 6, Hooks::FramePrologue};

// The puppet the player controls. Its motion controller at + 0x478 plays motions at + 0x70.
constexpr VAddr Puppet = 0x1f30190;
constexpr u64 PuppetMotions = 0x478;
constexpr u64 MotionsPlayer = 0x70;

// From the player: the rate of motion time, and the vector of layers (begin + 8, end + 0x10).
constexpr u64 PlayerRate = 0x40;
constexpr u64 PlayerLayers = 0x38;

// A layer: its rate, and its list of motions, whose node at + 0x28 is the last one or the list's
// head at + 8. A node points to its motion at + 0x18.
constexpr u64 LayerRate = 0x4c;
constexpr u64 LayerHead = 0x08;
constexpr u64 LayerLast = 0x28;
constexpr u64 NodeMotion = 0x18;

// A motion: its length and playhead in ticks, and how many more times it plays and has played,
// fewer than 0 for always.
constexpr u64 MotionLength = 0x38;
constexpr u64 MotionRepeats = 0x3c;
constexpr u64 MotionPlayhead = 0x40;
constexpr u64 MotionPlayed = 0x44;

constexpr float FrameRate = 30.0f;

using PlayFunction = u64 PS4_SYSV_ABI (*)(float blend, u64 player, u64 layer, u64 motion,
                                          u64 repeats, u64 flags);

VAddr g_module_base = 0;
PlayFunction g_play = nullptr;

template <typename T>
T Load(u64 address) {
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}

// The layer at index of Kat's motion player, or 0 when player is another one or has no such
// layer.
u64 KatLayer(u64 player, u32 index) {
    const u64 puppet = Load<u64>(g_module_base + Puppet - GoldHenBase);
    if (puppet == 0 || Load<u64>(puppet + PuppetMotions) + MotionsPlayer != player) {
        return 0;
    }
    const u64 layers = Load<u64>(player + PlayerLayers);
    const u64 begin = Load<u64>(layers + 8);
    if (index >= (Load<u64>(layers + 0x10) - begin) / sizeof(u64)) {
        return 0;
    }
    return Load<u64>(begin + index * sizeof(u64));
}

// Whether the layer's last motion has played to its end and holds its last pose.
bool LastMotionEnded(u64 layer) {
    const u64 node = Load<u64>(layer + LayerLast);
    if (node == 0 || node == layer + LayerHead) {
        return false;
    }
    const u64 motion = Load<u64>(node + NodeMotion);
    const s32 repeats = Load<s32>(motion + MotionRepeats);
    return repeats >= 0 && Load<s32>(motion + MotionPlayed) >= repeats &&
           Load<s32>(motion + MotionPlayhead) >= Load<s32>(motion + MotionLength);
}

// Most bones of Kat's strides hold the first pose until tick 100, where 30 fps first shows the
// stride, and that pose is away from where the last stride ends: above 30 fps they jump to it on
// one frame and stand still until tick 100. A motion of Kat's that replaces an ended one at once
// fades in from its last pose over one 30 fps frame of motion time instead, which moves them
// steadily to the pose 30 fps shows next. The ended motion adds nothing to Kat's travel.
u64 PS4_SYSV_ABI OnPlay(float blend, u64 player, u64 layer, u64 motion, u64 repeats, u64 flags) {
    if (blend == 0.0f) {
        const u64 kat_layer = KatLayer(player, static_cast<u32>(layer));
        if (kat_layer != 0 && LastMotionEnded(kat_layer)) {
            blend =
                Load<float>(player + PlayerRate) * Load<float>(kat_layer + LayerRate) / FrameRate;
        }
    }
    return g_play(blend, player, layer, motion, repeats, flags);
}

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
    g_module_base = module_base;
    const void* original = nullptr;
    if (Hooks::Replace(module_base, Play, c, capacity, reinterpret_cast<const void*>(&OnPlay),
                       &original) != Hooks::Result::Hooked) {
        LOG_ERROR(Render_Vulkan, "GR2port: the motions' play is not the expected code");
        return false;
    }
    g_play = reinterpret_cast<PlayFunction>(const_cast<void*>(original));
    return true;
}

} // namespace Gr2Port::MotionEnds
