// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <bit>
#include <xbyak/xbyak.h>

#include "common/logging/log.h"
#include "graphics/gr2port/frame_windows.h"
#include "graphics/gr2port/hooks.h"

namespace Gr2Port::FrameWindows {

namespace {

// The frame rate patches' flag of a frame that completes a 30 fps frame, and the frame's length in
// 30 fps frames, at most 1.
constexpr VAddr WholeFrame = 0x1f7c608;
constexpr VAddr FrameShare = 0x1f7c600;

// FUN_00fddec0 counts an enemy's frames on a moving platform up to 30, and the enemy attaches once
// the count passes 2. After cmp ecx, 29: jg past the count; inc ecx; mov [rdx], ecx.
constexpr Hooks::Entry AttachCount{0xfde981, 6, {0x7f, 0x26, 0xff, 0xc1, 0x89, 0x0a}};
constexpr VAddr AttachCounted = 0xfde9a9;
constexpr VAddr AttachCountNext = 0xfde987;

// FUN_0111e2a0 raises in r8d how many frames Dusty has followed a platform, and follows it while
// the count is below 2: mov [r14 + 0x210], r8d.
constexpr Hooks::Entry FollowCount{0x111e65b, 7, {0x45, 0x89, 0x86, 0x10, 0x02, 0x00, 0x00}};
constexpr VAddr FollowCountNext = 0x111e662;
constexpr u32 FollowCountOffset = 0x210;

// FUN_015b0340 adds the frame's ticks, which stay in xmm0, to a run's ticks in eax and ends its
// ledge window at 5000: cmp eax, 4999 before ja, and cmp eax, 5000 before jb.
constexpr Hooks::Entry LedgeEnd{0x15b0cce, 5, {0x3d, 0x87, 0x13, 0x00, 0x00}};
constexpr VAddr LedgeEndNext = 0x15b0cd3;
constexpr u32 LedgeEndLimit = 4999;
constexpr Hooks::Entry LedgeProbe{0x15b1b25, 5, {0x3d, 0x88, 0x13, 0x00, 0x00}};
constexpr VAddr LedgeProbeNext = 0x15b1b2a;
constexpr u32 LedgeProbeLimit = 5000;

// FUN_0085e330: mov esi, 33330, the microseconds its job queues may run in a frame while the world
// is stopped. The flags of the compare before it are read after it.
constexpr Hooks::Entry StoppedBudget{0x861023, 5, {0xbe, 0x32, 0x82, 0x00, 0x00}};
constexpr VAddr StoppedBudgetNext = 0x861028;
constexpr float StoppedMicroseconds = 33330.0f;

// FUN_01061b80 moves a bird each frame by its speed, a distance per 30 fps frame:
// vmulss xmm9, xmm1, [r14 + 0x130].
constexpr Hooks::Entry BirdSpeed{
    0x1062839, 9, {0xc4, 0x41, 0x72, 0x59, 0x8e, 0x30, 0x01, 0x00, 0x00}};
constexpr VAddr BirdSpeedNext = 0x1062842;
constexpr u32 BirdSpeedOffset = 0x130;

constexpr size_t StubBytes = 192;

} // namespace

bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity) {
    using namespace Xbyak::util;
    const std::array entries{AttachCount, FollowCount,   LedgeEnd,
                             LedgeProbe,  StoppedBudget, BirdSpeed};
    if (!std::ranges::all_of(entries, [module_base](const Hooks::Entry& entry) {
            return Hooks::Holds(module_base, entry);
        })) {
        LOG_ERROR(Render_Vulkan, "GR2port: the frame windows' code is not the expected code");
        return false;
    }
    if (capacity - c.getSize() < StubBytes + entries.size() * Hooks::MaxStubBytes) {
        LOG_ERROR(Render_Vulkan, "GR2port: no room for the frame windows' hooks");
        return false;
    }
    const auto code = [module_base](VAddr address) { return Hooks::Code(module_base, address); };

    const void* attach = c.getCurr();
    c.jg(code(AttachCounted));
    c.test(byte[rip + code(WholeFrame)], 1);
    c.jz(code(AttachCountNext));
    c.inc(ecx);
    c.mov(dword[rdx], ecx);
    c.jmp(code(AttachCountNext));

    const void* follow = c.getCurr();
    Xbyak::Label follow_held;
    c.test(byte[rip + code(WholeFrame)], 1);
    c.jz(follow_held);
    c.mov(dword[r14 + FollowCountOffset], r8d);
    c.L(follow_held);
    c.jmp(code(FollowCountNext));

    // The window ends on the frame whose ticks come nearest its length.
    const auto ledge = [&](u32 limit, VAddr next) {
        const void* stub = c.getCurr();
        c.push(rcx);
        c.vcvttss2si(ecx, xmm0);
        c.shr(ecx, 1);
        c.add(ecx, eax);
        c.cmp(ecx, limit);
        c.pop(rcx);
        c.jmp(code(next));
        return stub;
    };
    const void* ledge_end = ledge(LedgeEndLimit, LedgeEndNext);
    const void* ledge_probe = ledge(LedgeProbeLimit, LedgeProbeNext);

    const u8* const microseconds = c.getCurr();
    c.dd(std::bit_cast<u32>(StoppedMicroseconds));
    const void* budget = c.getCurr();
    c.lea(rsp, ptr[rsp - 16]);
    c.vmovups(ptr[rsp], xmm0);
    c.vmovss(xmm0, dword[rip + code(FrameShare)]);
    c.vmulss(xmm0, xmm0, dword[rip + microseconds]);
    c.vcvttss2si(esi, xmm0);
    c.vmovups(xmm0, ptr[rsp]);
    c.lea(rsp, ptr[rsp + 16]);
    c.jmp(code(StoppedBudgetNext));

    const void* bird = c.getCurr();
    c.vmulss(xmm9, xmm1, dword[r14 + BirdSpeedOffset]);
    c.vmulss(xmm9, xmm9, dword[rip + code(FrameShare)]);
    c.jmp(code(BirdSpeedNext));

    if (Hooks::Replace(module_base, AttachCount, c, capacity, attach) != Hooks::Result::Hooked ||
        Hooks::Replace(module_base, FollowCount, c, capacity, follow) != Hooks::Result::Hooked ||
        Hooks::Replace(module_base, LedgeEnd, c, capacity, ledge_end) != Hooks::Result::Hooked ||
        Hooks::Replace(module_base, LedgeProbe, c, capacity, ledge_probe) !=
            Hooks::Result::Hooked ||
        Hooks::Replace(module_base, StoppedBudget, c, capacity, budget) != Hooks::Result::Hooked ||
        Hooks::Replace(module_base, BirdSpeed, c, capacity, bird) != Hooks::Result::Hooked) {
        LOG_ERROR(Render_Vulkan, "GR2port: the frame windows' hooks did not go in");
        return false;
    }
    return true;
}

} // namespace Gr2Port::FrameWindows
