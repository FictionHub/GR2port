// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <xbyak/xbyak.h>

#include "common/logging/log.h"
#include "graphics/gr2port/hooks.h"
#include "graphics/gr2port/settings.h"
#include "graphics/gr2port/special_cameras.h"

namespace Gr2Port::SpecialCameras {

namespace {

// FUN_0156dce0, the camera of the Normal style's Special Attack flight: vmulss xmm3, xmm3, [0.05]
// makes the turn of this frame from the square of the right stick's tilt. The game writes the
// flags before it reads them.
constexpr Hooks::Entry FlightTurn{0x156def9, 8, {0xc5, 0xe2, 0x59, 0x1d, 0x87, 0x82, 0x35, 0x00}};
constexpr VAddr FlightTurnScale = 0x18c6188;

// FUN_008c8eb0, the camera of Panther Mode and the Gravity Slide: vmulss xmm0, xmm0, [-0.1] makes
// the yaw of this frame from the right stick's tilt. The game writes the flags before it reads
// them.
constexpr Hooks::Entry SlideTurn{0x8c95c5, 8, {0xc5, 0xfa, 0x59, 0x05, 0xbf, 0xe6, 0xfe, 0x00}};
constexpr VAddr SlideTurnScale = 0x18b7c8c;

// The same camera then moves the turned camera back toward where it was before the turn: 0.35 of
// the way at 30 fps, and at more than 30 fps the fps patches' SlideBlend of the way. A 30 fps frame
// keeps SlideKeep of its turn.
constexpr VAddr SlideBlend = 0x1f7c7b0;
constexpr float SlideKeep = 0.65f;

// The fps patches' share of a 30 fps frame that the last frame took, at most 1.
constexpr VAddr FrameShare = 0x1f7c600;

// The player: the object at PlayerObject in the holder's, a character when its type is
// CharacterType. Its Panther Mode is not 0 from the mode's start to the end of its exit.
constexpr VAddr PlayerHolder = 0x1f30190;
constexpr u32 PlayerObject = 0x588;
constexpr u32 ObjectType = 0x80;
constexpr u16 CharacterType = 5;
constexpr u32 PantherMode = 0x5e44;

// The highest sensitivity, twice the game's turn.
constexpr u32 MaxPercent = 200;

// The two stubs, their jumps from the sites, and the room the last jump's Replace asks for.
constexpr size_t StubBytes = 176 + 2 * 14 + Hooks::MaxStubBytes;

// The scales the stubs read and Set writes.
struct Scales {
    float special_attack = 1.0f;
    float panther = 1.0f;
};
Scales g_scales;
static_assert(std::atomic_ref<float>::is_always_lock_free);

float Scale(u32 percent) {
    return static_cast<float>(std::min(percent, MaxPercent)) / 100.0f;
}

} // namespace

bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity) {
    using namespace Xbyak::util;
    if (!Hooks::Holds(module_base, FlightTurn) || !Hooks::Holds(module_base, SlideTurn)) {
        LOG_ERROR(Render_Vulkan, "GR2port: the special cameras' turns are not the expected code");
        return false;
    }
    if (capacity - c.getSize() < StubBytes) {
        LOG_ERROR(Render_Vulkan, "GR2port: no room for the hooks of the special cameras' turns");
        return false;
    }
    const Settings::Values settings = Settings::Get();
    Set(settings.mouse.special_attack_camera, settings.mouse.panther_camera);
    // FrameShare holds the frame's share only with the fps patches of more than 30 fps.
    const bool share = settings.game.frame_rate > 30;
    const auto code = [module_base](VAddr address) { return Hooks::Code(module_base, address); };

    const void* flight = c.getCurr();
    c.vmulss(xmm3, xmm3, dword[rip + code(FlightTurnScale)]);
    if (share) {
        c.vmulss(xmm3, xmm3, dword[rip + code(FrameShare)]);
    }
    c.push(rax);
    c.mov(rax, reinterpret_cast<u64>(&g_scales));
    c.vmulss(xmm3, xmm3, dword[rax + offsetof(Scales, special_attack)]);
    c.pop(rax);
    c.jmp(code(FlightTurn.address + FlightTurn.size));

    Xbyak::Label done;
    Xbyak::Label one;
    Xbyak::Label keep;
    const void* slide = c.getCurr();
    c.vmulss(xmm0, xmm0, dword[rip + code(SlideTurnScale)]);
    if (share) {
        // A frame turns its share of a 30 fps frame's turn, and keeps as much of it as a 30 fps
        // frame keeps. The game writes xmm2 after the site before it reads it.
        c.vmulss(xmm0, xmm0, dword[rip + code(FrameShare)]);
        c.vmovss(xmm2, dword[rip + one]);
        c.vsubss(xmm2, xmm2, dword[rip + code(SlideBlend)]);
        c.vdivss(xmm0, xmm0, xmm2);
        c.vmulss(xmm0, xmm0, dword[rip + keep]);
    }
    c.push(rax);
    c.mov(rax, qword[rip + code(PlayerHolder)]);
    c.test(rax, rax);
    c.jz(done);
    c.mov(rax, qword[rax + PlayerObject]);
    c.test(rax, rax);
    c.jz(done);
    c.cmp(word[rax + ObjectType], CharacterType);
    c.jne(done);
    c.cmp(dword[rax + PantherMode], 0);
    c.je(done);
    c.mov(rax, reinterpret_cast<u64>(&g_scales));
    c.vmulss(xmm0, xmm0, dword[rax + offsetof(Scales, panther)]);
    c.L(done);
    c.pop(rax);
    c.jmp(code(SlideTurn.address + SlideTurn.size));
    c.L(one);
    c.dd(std::bit_cast<u32>(1.0f));
    c.L(keep);
    c.dd(std::bit_cast<u32>(SlideKeep));

    if (Hooks::Replace(module_base, FlightTurn, c, capacity, flight) != Hooks::Result::Hooked ||
        Hooks::Replace(module_base, SlideTurn, c, capacity, slide) != Hooks::Result::Hooked) {
        LOG_ERROR(Render_Vulkan, "GR2port: the special cameras' turns could not be hooked");
        return false;
    }
    return true;
}

void Set(u32 special_attack, u32 panther) {
    std::atomic_ref<float>(g_scales.special_attack)
        .store(Scale(special_attack), std::memory_order_relaxed);
    std::atomic_ref<float>(g_scales.panther).store(Scale(panther), std::memory_order_relaxed);
}

} // namespace Gr2Port::SpecialCameras
