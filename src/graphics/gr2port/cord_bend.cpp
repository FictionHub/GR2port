// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <xbyak/xbyak.h>

#include "common/logging/log.h"
#include "graphics/gr2port/cord_bend.h"
#include "graphics/gr2port/hooks.h"

namespace Gr2Port::CordBend {

namespace {

// FUN_014f66b0, a Himo chain's step, in its pass over the nodes and the nodes after next:
// vmulss xmm1, xmm1, [rdi] multiplies the pair's gap by the spring's share, and add rdi, 4 moves
// to the next pair's share.
constexpr Hooks::Entry SpringShare{0x14f7713, 8, {0xc5, 0xf2, 0x59, 0x0f, 0x48, 0x83, 0xc7, 0x04}};
constexpr VAddr SpringShareNext = 0x14f771b;

// The frame rate patches' length of this frame in 30 fps frames, at most 1.
constexpr VAddr FrameShare = 0x1f7c600;

constexpr size_t StubBytes = 32;

} // namespace

bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity) {
    using namespace Xbyak::util;
    if (!Hooks::Holds(module_base, SpringShare)) {
        LOG_ERROR(Render_Vulkan, "GR2port: the Himo chain's spring is not the expected code");
        return false;
    }
    if (capacity - c.getSize() < StubBytes + Hooks::MaxStubBytes) {
        LOG_ERROR(Render_Vulkan, "GR2port: no room for the Himo chain's spring hook");
        return false;
    }
    const auto code = [module_base](VAddr address) { return Hooks::Code(module_base, address); };
    const void* stub = c.getCurr();
    c.vmulss(xmm1, xmm1, dword[rdi]);
    c.vmulss(xmm1, xmm1, dword[rip + code(FrameShare)]);
    c.add(rdi, 4);
    c.jmp(code(SpringShareNext));
    if (Hooks::Replace(module_base, SpringShare, c, capacity, stub) != Hooks::Result::Hooked) {
        LOG_ERROR(Render_Vulkan, "GR2port: the Himo chain's spring hook did not go in");
        return false;
    }
    return true;
}

} // namespace Gr2Port::CordBend
