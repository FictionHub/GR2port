// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <xbyak/xbyak.h>

#include "common/logging/log.h"
#include "graphics/gr2port/hooks.h"
#include "graphics/gr2port/screen_shake.h"
#include "graphics/gr2port/settings.h"

namespace Gr2Port::ScreenShake {

namespace {

// FUN_015d7100, the camera manager's frame in r15, once it has added the shakes up.
// vmovaps xmm1, [r15 + 0x320] loads the turn the up vector takes; xmm0, xmm2, xmm3 and the flags
// are written before they are read.
constexpr Hooks::Entry ManagerTurn{
    0x15da384, 9, {0xc4, 0xc1, 0x78, 0x28, 0x8f, 0x20, 0x03, 0x00, 0x00}};
// vmovaps xmm2, [r15 + 0x300] loads the offset the eye and the target take.
constexpr Hooks::Entry ManagerOffset{
    0x15da47c, 9, {0xc4, 0xc1, 0x78, 0x28, 0x97, 0x00, 0x03, 0x00, 0x00}};
// cmp byte [r15 + 0x330], 0 tests the pad's link to the camera, whose rumble takes the size of
// the offset in xmm2; it reloads xmm0 and xmm1 itself.
constexpr Hooks::Entry ManagerRumble{
    0x15da4a7, 8, {0x41, 0x80, 0xbf, 0x30, 0x03, 0x00, 0x00, 0x00}};

// FUN_009d1130, the event camera node's view, with the manager in rax: the same three steps. The
// node's FUN_009d0b60 makes it the event camera through FUN_009cf860.
// vmovaps xmm0, [rax + 0x320]; xmm2, xmm3 and the flags are written before they are read.
constexpr Hooks::Entry EventTurn{0x9d1979, 8, {0xc5, 0xf8, 0x28, 0x80, 0x20, 0x03, 0x00, 0x00}};
// vmovaps xmm2, [rax + 0x300].
constexpr Hooks::Entry EventOffset{0x9d1a5f, 8, {0xc5, 0xf8, 0x28, 0x90, 0x00, 0x03, 0x00, 0x00}};
// cmp byte [rax + 0x330], 0.
constexpr Hooks::Entry EventRumble{0x9d1a8f, 7, {0x80, 0xb8, 0x30, 0x03, 0x00, 0x00, 0x00}};

// The manager's sums of the shakes: the turn, a quaternion x, y, z, w, and the offset; and its
// link of the pad's vibration to the camera.
constexpr u32 Turn = 0x320;
constexpr u32 Offset = 0x300;
constexpr u32 PadLink = 0x330;

// The six stubs, their jumps from the sites, and the room the last jump's Replace asks for.
constexpr size_t StubBytes = 320 + 6 * 14 + Hooks::MaxStubBytes;

// The scale the stubs read and Set writes, and the turn of no shake.
struct alignas(16) Shake {
    std::array<float, 4> none{0.0f, 0.0f, 0.0f, 1.0f};
    float scale = 1.0f;
};
Shake g_shake;
static_assert(std::atomic_ref<float>::is_always_lock_free);

// Loads into turn the manager's turn at [manager + Turn], scaled toward no turn: the normalized
// scale * turn + (1 - scale) * none, which keeps the game's own turn at scale 1. spare is kept;
// a and b and the flags are written.
void EmitTurn(Xbyak::CodeGenerator& c, const Xbyak::Reg64& manager, const Xbyak::Xmm& turn,
              const Xbyak::Xmm& a, const Xbyak::Xmm& b, const Xbyak::Reg64& spare,
              const void* back) {
    using namespace Xbyak::util;
    Xbyak::Label own;
    c.push(spare);
    c.mov(spare, reinterpret_cast<u64>(&g_shake));
    c.vbroadcastss(a, dword[spare + offsetof(Shake, scale)]);
    c.vmovaps(b, xword[spare + offsetof(Shake, none)]);
    c.vucomiss(a, dword[spare + offsetof(Shake, none) + 3 * sizeof(float)]);
    c.pop(spare);
    c.je(own);
    c.vmulps(turn, a, xword[manager + Turn]);
    c.vmulps(a, b, a);
    c.vsubps(a, b, a);
    c.vaddps(turn, turn, a);
    c.vdpps(a, turn, turn, 0xff);
    c.vsqrtps(a, a);
    c.vdivps(turn, turn, a);
    c.jmp(back);
    c.L(own);
    c.vmovaps(turn, xword[manager + Turn]);
    c.jmp(back);
}

// Loads into xmm2 the manager's offset times the scale. spare is kept.
void EmitOffset(Xbyak::CodeGenerator& c, const Xbyak::Reg64& manager, const Xbyak::Reg64& spare,
                const void* back) {
    using namespace Xbyak::util;
    c.push(spare);
    c.mov(spare, reinterpret_cast<u64>(&g_shake));
    c.vbroadcastss(xmm2, dword[spare + offsetof(Shake, scale)]);
    c.pop(spare);
    c.vmulps(xmm2, xmm2, xword[manager + Offset]);
    c.jmp(back);
}

// Loads the manager's own offset into xmm2 again for the rumble, then tests the pad's link.
void EmitRumble(Xbyak::CodeGenerator& c, const Xbyak::Reg64& manager, const void* back) {
    using namespace Xbyak::util;
    c.vmovaps(xmm2, xword[manager + Offset]);
    c.cmp(byte[manager + PadLink], 0);
    c.jmp(back);
}

} // namespace

bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity) {
    using namespace Xbyak::util;
    constexpr std::array Sites{&ManagerTurn, &ManagerOffset, &ManagerRumble,
                               &EventTurn,   &EventOffset,   &EventRumble};
    if (!std::ranges::all_of(Sites, [module_base](const Hooks::Entry* site) {
            return Hooks::Holds(module_base, *site);
        })) {
        LOG_ERROR(Render_Vulkan, "GR2port: the cameras' shake is not the expected code");
        return false;
    }
    if (capacity - c.getSize() < StubBytes) {
        LOG_ERROR(Render_Vulkan, "GR2port: no room for the hooks of the screen shake");
        return false;
    }
    Set(Settings::Get().graphics.screen_shake);
    const auto back = [module_base](const Hooks::Entry& site) {
        return reinterpret_cast<const void*>(module_base + site.address + site.size -
                                             Hooks::CodeBase);
    };

    std::array<const void*, Sites.size()> stubs{};
    stubs[0] = c.getCurr();
    EmitTurn(c, r15, xmm1, xmm0, xmm2, rax, back(ManagerTurn));
    stubs[1] = c.getCurr();
    EmitOffset(c, r15, rax, back(ManagerOffset));
    stubs[2] = c.getCurr();
    EmitRumble(c, r15, back(ManagerRumble));
    stubs[3] = c.getCurr();
    EmitTurn(c, rax, xmm0, xmm2, xmm3, rcx, back(EventTurn));
    stubs[4] = c.getCurr();
    EmitOffset(c, rax, rcx, back(EventOffset));
    stubs[5] = c.getCurr();
    EmitRumble(c, rax, back(EventRumble));
    for (size_t i = 0; i < Sites.size(); ++i) {
        Hooks::Replace(module_base, *Sites[i], c, capacity, stubs[i]);
    }
    return true;
}

void Set(u32 percent) {
    std::atomic_ref<float>(g_shake.scale)
        .store(static_cast<float>(percent) / 100.0f, std::memory_order_relaxed);
}

} // namespace Gr2Port::ScreenShake
