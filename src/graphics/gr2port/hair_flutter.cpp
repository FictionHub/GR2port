// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <bit>
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

// FUN_01590f20, while Kat floats: vmovss xmm0, [0x18c6904] loads 0.045, the air that
// FUN_01523db0 then pushes along her hair in each frame.
constexpr Hooks::Entry FloatPush{0x15931d8, 8, {0xc5, 0xfa, 0x10, 0x05, 0x24, 0x37, 0x33, 0x00}};
constexpr VAddr FloatPushNext = 0x15931e0;
constexpr VAddr FloatPushAmount = 0x18c6904;

// The frames per second the limit and the push are made for.
constexpr float GameRate = 30.0f;

// The two stubs and their constant.
constexpr size_t StubBytes = 96;

} // namespace

bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity) {
    using namespace Xbyak::util;
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
    const void* push = c.getCurr();
    c.vmovss(xmm0, dword[rip + code(FloatPushAmount)]);
    c.vmulss(xmm0, xmm0, dword[rip + code(StepTime)]);
    c.vmulss(xmm0, xmm0, dword[rip + game_rate]);
    c.jmp(code(FloatPushNext));
    c.L(game_rate);
    c.dd(std::bit_cast<u32>(GameRate));
    if (Hooks::Replace(module_base, Limit, c, capacity, limit) != Hooks::Result::Hooked) {
        LOG_ERROR(Render_Vulkan, "GR2port: the hair's step is not the expected code");
        return false;
    }
    if (Hooks::Replace(module_base, FloatPush, c, capacity, push) != Hooks::Result::Hooked) {
        LOG_ERROR(Render_Vulkan, "GR2port: the float's push on the hair is not the expected code");
        return false;
    }
    return true;
}

} // namespace Gr2Port::HairFlutter
