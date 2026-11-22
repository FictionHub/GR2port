// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <bit>
#include <xbyak/xbyak.h>

#include "common/logging/log.h"
#include "graphics/gr2port/gravity_slide.h"
#include "graphics/gr2port/hooks.h"
#include "graphics/gr2port/settings.h"

namespace Gr2Port::GravitySlide {

namespace {

// The game step's time in seconds, a float.
constexpr VAddr StepTime = 0x1efb104;

// The frame rate patches' length of this game frame in 30 fps frames, at most 1. The step's time
// over this is a 30 fps frame at the current time scale.
constexpr VAddr FrameShare = 0x1f7c600;

// The frames per second the steering's steps are made for.
constexpr float StepRate = 30.0f;

// FUN_008b3860, the slide's update, with the steering in xmm1, the stick's target in xmm2 and
// the style's fraction in xmm0: vmovss xmm3, [1.0] starts steering = steering * (1 - fraction) +
// target * fraction, which ends with the vaddss at FollowSum. The game writes xmm0 and xmm3
// before it reads them again.
constexpr Hooks::Entry Follow{0x8b6075, 8, {0xc5, 0xfa, 0x10, 0x1d, 0xcb, 0x18, 0x00, 0x01}};
constexpr VAddr FollowSum = 0x8b6089;

// FUN_008c2900, the slide's turn, with the steering in xmm6 and the amount it eases back by in
// xmm1: vmovss [r15 + 0x324], xmm2 stores their sum. The game reads the amount in xmm1 later.
constexpr Hooks::Entry Ease{0x8c2f63, 9, {0xc4, 0xc1, 0x7a, 0x11, 0x97, 0x24, 0x03, 0x00, 0x00}};
constexpr VAddr EaseNext = 0x8c2f6c;
constexpr u32 Steering = 0x324;

// FUN_008b3860 again: vpermilps xmm0, [rsp + 0x170], 0 broadcasts the step that takes Kat's
// velocity to the point where the ground test looks.
constexpr Hooks::Entry LookAhead{
    0x8b85bd, 11, {0xc4, 0xe3, 0x79, 0x04, 0x84, 0x24, 0x70, 0x01, 0x00, 0x00, 0x00}};
constexpr VAddr LookAheadNext = 0x8b85c8;

// With the gap from that point to the ground in xmm0, the point in xmm1, Kat's position in xmm2
// and her velocity in xmm13: vmulps xmm0, xmm0, [share] takes 0.3 or 0.4 of the gap. The game adds
// the point, subtracts the position, and sets the velocity to cover the difference in this step.
// It writes xmm3 and xmm4 before it reads them again.
constexpr Hooks::Entry PullLess{0x8b870f, 8, {0xc5, 0xf8, 0x59, 0x05, 0xc9, 0x9f, 0xf8, 0x00}};
constexpr VAddr PullLessShare = 0x18426e0;
constexpr Hooks::Entry PullMore{0x8b8719, 8, {0xc5, 0xf8, 0x59, 0x05, 0xaf, 0x9f, 0xf8, 0x00}};
constexpr VAddr PullMoreShare = 0x18426d0;
constexpr VAddr PullNext = 0x8b8721;

// vsubps xmm1, xmm1, [r14 + 0x5f20] subtracts the position from the point, for the speed the game
// keeps along the new direction.
constexpr Hooks::Entry KeptMotion{
    0x8b87e1, 9, {0xc4, 0xc1, 0x70, 0x5c, 0x8e, 0x20, 0x5f, 0x00, 0x00}};
constexpr VAddr KeptMotionNext = 0x8b87ea;

// FUN_00bc5cb0, the job that breaks a panel of glass after Kat touches it: vmovss [rcx + 0x18],
// xmm0 stores the job's time. The game does not read xmm0 again.
constexpr Hooks::Entry BreakTime{0xbc5f97, 5, {0xc5, 0xfa, 0x11, 0x41, 0x18}};
constexpr VAddr BreakTimeNext = 0xbc5f9c;
constexpr u32 JobTime = 0x18;

// The frames that pass beyond the job's time before the ground goes from under Kat: the job sees
// the touch a frame late, and the ground test sees the break a frame late.
constexpr float BreakFrames = 2.0f;

// The stack bytes a stub moves down by to keep a ymm register: the red zone and the register.
constexpr u32 KeptBytes = 0xa0;

// The stubs and their constants.
constexpr size_t StubBytes = 480;

} // namespace

bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity) {
    using namespace Xbyak::util;
    const auto code = [module_base](VAddr address) { return Hooks::Code(module_base, address); };
    if (capacity - c.getSize() < StubBytes) {
        LOG_ERROR(Render_Vulkan, "GR2port: no room for the gravity slide's hooks");
        return false;
    }
    Xbyak::Label step_rate, one, two, half, third;

    // With f the step's time in 30 fps frames, a fraction a of a frame becomes 1 - (1 - a)^f, by
    // its series f * a * (1 + (1 - f) * a / 2 * (1 + (2 - f) * a / 3)): at 30 fps it is a itself.
    const void* follow = c.getCurr();
    c.lea(rsp, ptr[rsp - KeptBytes]);
    c.vmovups(ptr[rsp], ymm4);
    c.vmovss(xmm3, dword[rip + code(StepTime)]);
    c.vmulss(xmm3, xmm3, dword[rip + step_rate]);
    c.vminss(xmm3, xmm3, dword[rip + one]);
    c.vmovss(xmm4, dword[rip + two]);
    c.vsubss(xmm4, xmm4, xmm3);
    c.vmulss(xmm4, xmm4, xmm0);
    c.vmulss(xmm4, xmm4, dword[rip + third]);
    c.vaddss(xmm4, xmm4, dword[rip + one]);
    c.vmulss(xmm4, xmm4, xmm0);
    c.vmulss(xmm4, xmm4, dword[rip + half]);
    c.vmulss(xmm0, xmm0, xmm3);
    c.vsubss(xmm3, xmm3, dword[rip + one]);
    c.vmulss(xmm4, xmm4, xmm3);
    c.vmovss(xmm3, dword[rip + one]);
    c.vsubss(xmm3, xmm3, xmm4);
    c.vmulss(xmm0, xmm0, xmm3);
    // The game's own step, with the new fraction in xmm0.
    c.vmovss(xmm3, dword[rip + one]);
    c.vsubss(xmm3, xmm3, xmm0);
    c.vmulss(xmm0, xmm2, xmm0);
    c.vmulss(xmm1, xmm1, xmm3);
    c.vmovups(ymm4, ptr[rsp]);
    c.lea(rsp, ptr[rsp + KeptBytes]);
    c.jmp(code(FollowSum));

    // The amount times f, then the game's own sum and store.
    const void* ease = c.getCurr();
    c.vmovss(xmm2, dword[rip + code(StepTime)]);
    c.vmulss(xmm2, xmm2, dword[rip + step_rate]);
    c.vminss(xmm2, xmm2, dword[rip + one]);
    c.vmulss(xmm1, xmm1, xmm2);
    c.vaddss(xmm2, xmm6, xmm1);
    c.vmovss(dword[r15 + Steering], xmm2);
    c.jmp(code(EaseNext));

    c.L(step_rate);
    c.dd(std::bit_cast<u32>(StepRate));
    c.L(one);
    c.dd(std::bit_cast<u32>(1.0f));
    c.L(two);
    c.dd(std::bit_cast<u32>(2.0f));
    c.L(half);
    c.dd(std::bit_cast<u32>(0.5f));
    c.L(third);
    c.dd(std::bit_cast<u32>(1.0f / 3.0f));

    if (Hooks::Replace(module_base, Follow, c, capacity, follow) != Hooks::Result::Hooked ||
        Hooks::Replace(module_base, Ease, c, capacity, ease) != Hooks::Result::Hooked) {
        LOG_ERROR(Render_Vulkan, "GR2port: the gravity slide's steering is not the expected code");
        return false;
    }

    // At the game's own 30 fps the slide keeps the stock ground step.
    if (Settings::Get().game.frame_rate <= 30) {
        return true;
    }
    Xbyak::Label break_frames;

    // The ground test looks as far ahead as in a 30 fps frame.
    const void* look_ahead = c.getCurr();
    c.vmovss(xmm0, dword[rip + code(StepTime)]);
    c.vdivss(xmm0, xmm0, dword[rip + code(FrameShare)]);
    c.vshufps(xmm0, xmm0, xmm0, 0);
    c.jmp(code(LookAheadNext));

    // A step of f 30 fps frames takes f of a frame's change of speed, which is f^2 of the share of
    // the gap. The point and the position become the step's motion and zero, because at a few
    // hundred frames a second the motion of a slow slide is a few units in the last place of a
    // position, and a velocity rebuilt from their difference stops growing.
    const auto pull = [&](VAddr share) {
        const void* stub = c.getCurr();
        c.vbroadcastss(xmm1, dword[rip + code(StepTime)]);
        c.vmulps(xmm1, xmm1, xmm13);
        c.vxorps(xmm2, xmm2, xmm2);
        c.vbroadcastss(xmm3, dword[rip + code(FrameShare)]);
        c.vmulps(xmm0, xmm0, xmm3);
        c.vmulps(xmm0, xmm0, xmm3);
        c.vmulps(xmm0, xmm0, xword[rip + code(share)]);
        c.jmp(code(PullNext));
        return stub;
    };
    const void* pull_less = pull(PullLessShare);
    const void* pull_more = pull(PullMoreShare);

    // xmm1 already holds the step's motion.
    const void* kept_motion = c.getCurr();
    c.jmp(code(KeptMotionNext));

    // The job's time gains what its frames lose against 30 fps frames.
    const void* break_time = c.getCurr();
    c.lea(rsp, ptr[rsp - KeptBytes]);
    c.vmovups(ptr[rsp], ymm1);
    c.vmovss(xmm1, dword[rip + code(StepTime)]);
    c.vdivss(xmm1, xmm1, dword[rip + code(FrameShare)]);
    c.vsubss(xmm1, xmm1, dword[rip + code(StepTime)]);
    c.vmulss(xmm1, xmm1, dword[rip + break_frames]);
    c.vaddss(xmm0, xmm0, xmm1);
    c.vmovups(ymm1, ptr[rsp]);
    c.lea(rsp, ptr[rsp + KeptBytes]);
    c.vmovss(dword[rcx + JobTime], xmm0);
    c.jmp(code(BreakTimeNext));

    c.L(break_frames);
    c.dd(std::bit_cast<u32>(BreakFrames));

    if (Hooks::Replace(module_base, LookAhead, c, capacity, look_ahead) != Hooks::Result::Hooked ||
        Hooks::Replace(module_base, PullLess, c, capacity, pull_less) != Hooks::Result::Hooked ||
        Hooks::Replace(module_base, PullMore, c, capacity, pull_more) != Hooks::Result::Hooked ||
        Hooks::Replace(module_base, KeptMotion, c, capacity, kept_motion) !=
            Hooks::Result::Hooked) {
        LOG_ERROR(Render_Vulkan, "GR2port: the gravity slide's ground is not the expected code");
        return false;
    }
    if (Hooks::Replace(module_base, BreakTime, c, capacity, break_time) != Hooks::Result::Hooked) {
        LOG_ERROR(Render_Vulkan, "GR2port: the glass's break time is not the expected code");
        return false;
    }
    return true;
}

} // namespace Gr2Port::GravitySlide
