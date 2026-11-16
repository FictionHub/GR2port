// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <utility>
#include <xbyak/xbyak.h>

#include "common/logging/log.h"
#include "graphics/gr2port/comic_camera.h"
#include "graphics/gr2port/hooks.h"

namespace Gr2Port::ComicCamera {

namespace {

// The game step's time in seconds, a float.
constexpr VAddr StepTime = 0x1efb104;

// The frames per second the move's counts and fractions are made for.
constexpr float GameRate = 30.0f;

// FUN_017742c0, a 3D comic page's update, with the page in rbx and the frames since the panel
// change in r12d: mov edx, [rbx + 0x2e4], mov eax, [rbx + 0x2e8] and mov ecx, [rbx + 0x2ec] load
// the frames before the fractions grow, the frames the angles wait and the frames the growth
// takes. The game writes xmm2 and xmm3 before it reads them again.
constexpr Hooks::Entry Counts{0x177585c, 6, {0x8b, 0x93, 0xe4, 0x02, 0x00, 0x00}};
constexpr std::array<u8, 12> LaterCounts{0x8b, 0x83, 0xe8, 0x02, 0x00, 0x00,
                                         0x8b, 0x8b, 0xec, 0x02, 0x00, 0x00};
constexpr VAddr CountsNext = 0x177586e;
constexpr u32 FramesBeforeGrowth = 0x2e4;
constexpr u32 AnglesWait = 0x2e8;
constexpr u32 GrowthFrames = 0x2ec;

// vpermilps xmm2, xmm7, 0 spreads the fraction of the way the camera's points move this frame,
// and vpermilps xmm1, xmm11, 0 the fraction its angles turn. The game later compares xmm11 with
// the page's threshold that starts the panel's animation.
constexpr Hooks::Entry Move{0x1775afd, 6, {0xc4, 0xe3, 0x79, 0x04, 0xd7, 0x00}};
constexpr VAddr MoveNext = 0x1775b03;
constexpr Hooks::Entry Turn{0x1775bc4, 6, {0xc4, 0xc3, 0x79, 0x04, 0xcb, 0x00}};
constexpr VAddr TurnNext = 0x1775bca;

// The fewest 30 fps frames a step's time is taken as.
constexpr float ShortestStep = 1.0f / 64.0f;

// The stack bytes a stub moves down by first: the red zone.
constexpr s32 RedZone = 128;

// The stubs and their constants.
constexpr size_t StubBytes = 1024;

VAddr g_module_base = 0;

// The game step's time in 30 fps frames, between ShortestStep and 1.
float StepFrames() {
    float step;
    std::memcpy(&step, Hooks::Code(g_module_base, StepTime), sizeof(step));
    const float frames = step * GameRate;
    return frames >= 1.0f ? 1.0f : frames > ShortestStep ? frames : ShortestStep;
}

// The fraction of the way the step moves when a 30 fps frame moves fraction of it:
// 1 - (1 - fraction)^f, with f the step's time in 30 fps frames. At 30 fps it is fraction itself.
float PS4_SYSV_ABI StepFraction(float fraction) {
    const float frames = StepFrames();
    if (frames >= 1.0f || !(fraction > 0.0f) || fraction >= 1.0f) {
        return fraction;
    }
    return 1.0f - std::pow(1.0f - fraction, frames);
}

// Writes a stub that calls StepFraction with the low float of from, leaves the result in each
// lane of to, as vpermilps would, and jumps to next. It keeps the flags and every other register.
const void* FractionStub(Xbyak::CodeGenerator& c, const Xbyak::Xmm& from, const Xbyak::Xmm& to,
                         const u8* next) {
    using namespace Xbyak::util;
    const std::array<Xbyak::Reg64, 9> saved{rax, rcx, rdx, rsi, rdi, r8, r9, r10, r11};
    constexpr u32 VectorRegisters = 16;
    constexpr u32 VectorBytes = 32;
    const void* stub = c.getCurr();
    c.lea(rsp, ptr[rsp - RedZone]);
    c.pushfq();
    for (const auto& reg : saved) {
        c.push(reg);
    }
    c.push(rbp);
    c.mov(rbp, rsp);
    c.sub(rsp, VectorRegisters * VectorBytes);
    c.and_(rsp, ~u32{VectorBytes - 1});
    for (u32 i = 0; i < VectorRegisters; i++) {
        c.vmovdqu(ptr[rsp + i * VectorBytes], Xbyak::Ymm(static_cast<int>(i)));
    }
    c.vmovaps(xmm0, from);
    c.mov(rax, reinterpret_cast<u64>(&StepFraction));
    c.call(rax);
    c.vshufps(xmm0, xmm0, xmm0, 0);
    c.vmovdqu(ptr[rsp + to.getIdx() * VectorBytes], ymm0);
    for (u32 i = 0; i < VectorRegisters; i++) {
        c.vmovdqu(Xbyak::Ymm(static_cast<int>(i)), ptr[rsp + i * VectorBytes]);
    }
    c.mov(rsp, rbp);
    c.pop(rbp);
    for (auto reg = saved.rbegin(); reg != saved.rend(); ++reg) {
        c.pop(*reg);
    }
    c.popfq();
    c.lea(rsp, ptr[rsp + RedZone]);
    c.jmp(next);
    return stub;
}

} // namespace

bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity) {
    using namespace Xbyak::util;
    g_module_base = module_base;
    const auto code = [module_base](VAddr address) { return Hooks::Code(module_base, address); };
    if (capacity - c.getSize() < StubBytes) {
        LOG_ERROR(Render_Vulkan, "GR2port: no room for the comic camera's hooks");
        return false;
    }
    if (std::memcmp(code(Counts.address + Counts.size), LaterCounts.data(), LaterCounts.size()) !=
        0) {
        LOG_ERROR(Render_Vulkan, "GR2port: the comic camera's counts are not the expected code");
        return false;
    }
    Xbyak::Label game_rate, one, shortest_step;

    // Each count of 30 fps frames becomes the count of steps that take as long.
    const void* counts = c.getCurr();
    c.vmovss(xmm2, dword[rip + code(StepTime)]);
    c.vmulss(xmm2, xmm2, dword[rip + game_rate]);
    c.vmaxss(xmm2, xmm2, dword[rip + shortest_step]);
    c.vminss(xmm2, xmm2, dword[rip + one]);
    c.vmovss(xmm3, dword[rip + one]);
    c.vdivss(xmm3, xmm3, xmm2);
    const std::array<std::pair<Xbyak::Reg32, u32>, 3> loads{std::pair{edx, FramesBeforeGrowth},
                                                            std::pair{eax, AnglesWait},
                                                            std::pair{ecx, GrowthFrames}};
    for (const auto& [reg, offset] : loads) {
        c.vcvtsi2ss(xmm2, xmm2, dword[rbx + offset]);
        c.vmulss(xmm2, xmm2, xmm3);
        c.vcvtss2si(reg, xmm2);
    }
    c.jmp(code(CountsNext));
    c.L(game_rate);
    c.dd(std::bit_cast<u32>(GameRate));
    c.L(one);
    c.dd(std::bit_cast<u32>(1.0f));
    c.L(shortest_step);
    c.dd(std::bit_cast<u32>(ShortestStep));

    const void* move = FractionStub(c, xmm7, xmm2, code(MoveNext));
    const void* turn = FractionStub(c, xmm11, xmm1, code(TurnNext));
    if (Hooks::Replace(module_base, Counts, c, capacity, counts) != Hooks::Result::Hooked ||
        Hooks::Replace(module_base, Move, c, capacity, move) != Hooks::Result::Hooked ||
        Hooks::Replace(module_base, Turn, c, capacity, turn) != Hooks::Result::Hooked) {
        LOG_ERROR(Render_Vulkan, "GR2port: the comic camera's move is not the expected code");
        return false;
    }
    return true;
}

} // namespace Gr2Port::ComicCamera
