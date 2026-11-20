// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <numbers>
#include <xbyak/xbyak.h>

#include "common/logging/log.h"
#include "graphics/gr2port/hair_flutter.h"
#include "graphics/gr2port/hooks.h"

namespace Gr2Port::HairFlutter {

namespace {

// The game step's time in seconds, a float.
constexpr VAddr StepTime = 0x1efb104;

// FUN_01501b20, with the air's mean speed along the chain in xmm0: vsubss xmm1, xmm0,
// [rbx + 0xa8] subtracts the chain's limit, the speed in a frame above which the hair flutters.
constexpr Hooks::Entry Limit{0x1501f08, 8, {0xc5, 0xfa, 0x5c, 0x8b, 0xa8, 0x00, 0x00, 0x00}};
constexpr VAddr LimitNext = 0x1501f10;

// FUN_01501b20, with the excess in xmm1 and the chain in rbx: vmulss xmm1, xmm1, [0x18c5804]
// multiplies the excess by 0.4 into the size of the wave's push on the middle node.
constexpr Hooks::Entry Size{0x1502307, 8, {0xc5, 0xf2, 0x59, 0x0d, 0xf5, 0x34, 0x3c, 0x00}};
constexpr VAddr SizeNext = 0x150230f;
constexpr float SizeFactor = 0.4f;

// The chain: its node count, its nodes' drags and its wave's rate. A step advances the wave's
// phase by the excess times PhaseFactor times the rate, in cycles.
constexpr u32 ChainNodes = 0x40;
constexpr u32 ChainDrags = 0x80;
constexpr u32 ChainRate = 0xb0;
constexpr float PhaseFactor = 0.8333334f;

// FUN_01590f20, while Kat floats: vmovss xmm0, [0x18c6904] loads 0.045, the air that
// FUN_01523db0 then pushes along her hair in each frame.
constexpr Hooks::Entry FloatPush{0x15931d8, 8, {0xc5, 0xfa, 0x10, 0x05, 0x24, 0x37, 0x33, 0x00}};
constexpr VAddr FloatPushNext = 0x15931e0;
constexpr VAddr FloatPushAmount = 0x18c6904;

// FUN_008f6430, while Kat + 0x5464 is set and Kat + 0x5400 is 0, with Kat's hair's root in rax:
// vaddps xmm0, xmm0, [rax + 0x70]; vmovaps [rax + 0x70], xmm0 adds to the root's pushes the air
// in xmm0, (0.01 cos, -0.02 - 0.03 sin, 0.01 sin) of a phase that the fps patch turns by the
// frame's time.
constexpr Hooks::Entry Swirl{
    0x8fadba, 10, {0xc5, 0xf8, 0x58, 0x40, 0x70, 0xc5, 0xf8, 0x29, 0x40, 0x70}};
constexpr VAddr SwirlNext = 0x8fadc4;

// The frames per second the limit and the pushes are made for.
constexpr float GameRate = 30.0f;

// The stack bytes a stub moves down by first: the red zone.
constexpr s32 RedZone = 128;

// The four stubs and their constant.
constexpr size_t StubBytes = 512;

VAddr g_module_base = 0;

// A node keeps 1 - drag of its speed in each step, gains the step's push and moves by its speed.
// For a push that turns by omega in each step, the node's swing is 1 / Response(drag, omega)
// times the push.
float Response(float drag, float omega) {
    const float keep = 1.0f - drag;
    const float cosine = std::cos(omega);
    return std::sqrt((1.0f - 2.0f * keep * cosine + keep * keep) * (1.0f - cosine));
}

// The size of the wave's push on chain's middle node for an excess of excess. Above 30 fps the
// air, the limit and so the excess are the step's share of a 30 fps frame's, and the steps keep
// the stock drag, so the wave that a 30 fps step barely follows moves the hair far more. The
// push takes the ratio of the swing at 30 fps to the swing at this step's time, for the wave's
// turn in a 30 fps frame and the middle node's drag, and never grows past the plain size.
float PS4_SYSV_ABI FlutterSize(float excess, const u8* chain) {
    const float size = excess * SizeFactor;
    float step;
    std::memcpy(&step, Hooks::Code(g_module_base, StepTime), sizeof(step));
    const float frames = step * GameRate;
    if (!(frames > 0.0f) || frames >= 1.0f || !(excess > 0.0f)) {
        return size;
    }
    s32 nodes;
    const u8* drags;
    float rate;
    float drag;
    std::memcpy(&nodes, chain + ChainNodes, sizeof(nodes));
    std::memcpy(&drags, chain + ChainDrags, sizeof(drags));
    std::memcpy(&rate, chain + ChainRate, sizeof(rate));
    std::memcpy(&drag, drags + ((nodes + 1) >> 1) * sizeof(drag), sizeof(drag));
    const float turn = 2.0f * std::numbers::pi_v<float> * excess * PhaseFactor * rate / frames;
    const float ratio = Response(drag, turn * frames) / (frames * Response(drag, turn));
    return ratio < 1.0f ? size * ratio : size;
}

// Writes a stub that sets the low lane of xmm1 to FlutterSize of it and of the chain in rbx, and
// jumps to next. It keeps the flags and every other register.
const void* SizeStub(Xbyak::CodeGenerator& c, const u8* next) {
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
    c.vmovaps(xmm0, xmm1);
    c.mov(rdi, rbx);
    c.mov(rax, reinterpret_cast<u64>(&FlutterSize));
    c.call(rax);
    c.vmovss(dword[rsp + 1 * VectorBytes], xmm0);
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
        LOG_ERROR(Render_Vulkan, "GR2port: no room for the hair's hook");
        return false;
    }
    Xbyak::Label game_rate;
    const void* limit = c.getCurr();
    c.vmovss(xmm1, dword[rbx + 0xa8]);
    c.vmulss(xmm1, xmm1, dword[rip + code(StepTime)]);
    c.vmulss(xmm1, xmm1, dword[rip + game_rate]);
    c.vsubss(xmm1, xmm0, xmm1);
    c.jmp(code(LimitNext));
    const void* size = SizeStub(c, code(SizeNext));
    const void* push = c.getCurr();
    c.vmovss(xmm0, dword[rip + code(FloatPushAmount)]);
    c.vmulss(xmm0, xmm0, dword[rip + code(StepTime)]);
    c.vmulss(xmm0, xmm0, dword[rip + game_rate]);
    c.jmp(code(FloatPushNext));
    const void* swirl = c.getCurr();
    c.lea(rsp, ptr[rsp - RedZone - 16]);
    c.vmovups(ptr[rsp], xmm1);
    c.vbroadcastss(xmm1, dword[rip + code(StepTime)]);
    c.vmulps(xmm0, xmm0, xmm1);
    c.vbroadcastss(xmm1, dword[rip + game_rate]);
    c.vmulps(xmm0, xmm0, xmm1);
    c.vmovups(xmm1, ptr[rsp]);
    c.lea(rsp, ptr[rsp + RedZone + 16]);
    c.vaddps(xmm0, xmm0, xword[rax + 0x70]);
    c.vmovaps(xword[rax + 0x70], xmm0);
    c.jmp(code(SwirlNext));
    c.L(game_rate);
    c.dd(std::bit_cast<u32>(GameRate));
    if (Hooks::Replace(module_base, Limit, c, capacity, limit) != Hooks::Result::Hooked) {
        LOG_ERROR(Render_Vulkan, "GR2port: the hair's step is not the expected code");
        return false;
    }
    if (Hooks::Replace(module_base, Size, c, capacity, size) != Hooks::Result::Hooked) {
        LOG_ERROR(Render_Vulkan, "GR2port: the hair's flutter is not the expected code");
        return false;
    }
    if (Hooks::Replace(module_base, FloatPush, c, capacity, push) != Hooks::Result::Hooked) {
        LOG_ERROR(Render_Vulkan, "GR2port: the float's push on the hair is not the expected code");
        return false;
    }
    if (Hooks::Replace(module_base, Swirl, c, capacity, swirl) != Hooks::Result::Hooked) {
        LOG_ERROR(Render_Vulkan, "GR2port: Kat's swirl on the hair is not the expected code");
        return false;
    }
    return true;
}

} // namespace Gr2Port::HairFlutter
