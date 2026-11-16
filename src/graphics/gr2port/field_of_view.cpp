// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <cstddef>
#include <xbyak/xbyak.h>

#include "common/logging/log.h"
#include "graphics/gr2port/field_of_view.h"
#include "graphics/gr2port/hooks.h"
#include "graphics/gr2port/settings.h"

namespace Gr2Port::FieldOfView {

namespace {

// FUN_015d7100, the camera manager's frame in r15: after FUN_015edda0 blends the cameras into the
// output at +0x1d0, mov eax, [r15 + 0x1d0]; mov [r14], eax keeps its field of view at +0x20, where
// the next frame's blend starts. eax, r14, xmm0 and the flags are written before they are read.
constexpr Hooks::Entry Output{
    0x15d93f9, 10, {0x41, 0x8b, 0x87, 0xd0, 0x01, 0x00, 0x00, 0x41, 0x89, 0x06}};
constexpr u64 OutputFieldOfView = 0x1d0;

// FUN_014cf0e0(aspect, matrix, parameters) builds a projection: vbroadcastss xmm3, [rsi] loads
// the parameters' field of view. It writes no general register.
constexpr Hooks::Entry Projection{0x14cf0e0, 5, {0xc4, 0xe2, 0x79, 0x18, 0x1e}};

// The returns of the calls that serve the Lua Query bindings scripts wait on for an object to
// leave the view. calcInSightAngleRatioGameObject calls FUN_014cf0e0 itself;
// calcInSightRatioGameObject and calcInSightRatioSphere call FUN_010e17a0, whose rbp frame holds
// their return while it calls FUN_014cf0e0.
constexpr VAddr AngleQueryReturn = 0x13ace66;
constexpr VAddr BoxProjectionReturn = 0x10e1a5c;
constexpr VAddr ObjectQueryReturn = 0x13ad15c;
constexpr VAddr SphereQueryReturn = 0x13ad2e5;

// The event camera node that holds the view, 0 for none. FUN_009cf860 sets it when a timeline or
// a script switches to its camera, and the node's release clears it.
constexpr VAddr EventCamera = 0x1ec3b20;

// The two stubs and their data.
constexpr size_t StubBytes = 256;

// The scale, and the last field of view the manager's stub wrote with the game's own it replaced.
// The stubs read and write it; Set writes the scale.
struct FieldOfViews {
    float scale = 1.0f;
    float scaled = 0.0f;
    float own = 0.0f;
};
FieldOfViews g_field_of_views;
static_assert(std::atomic_ref<float>::is_always_lock_free);

} // namespace

bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity) {
    using namespace Xbyak::util;
    const auto game = [module_base](VAddr address) {
        return module_base + address - Hooks::CodeBase;
    };
    if (capacity - c.getSize() < StubBytes) {
        LOG_ERROR(Render_Vulkan, "GR2port: no room for the hook of the field of view");
        return false;
    }
    Set(Settings::Get().graphics.field_of_view);
    const u64 views = reinterpret_cast<u64>(&g_field_of_views);

    // The manager's output takes the scaled field of view unless an event camera holds the view.
    Xbyak::Label output_back;
    const void* output = c.getCurr();
    c.mov(eax, dword[r15 + OutputFieldOfView]);
    c.mov(dword[r14], eax);
    c.mov(r14, game(EventCamera));
    c.cmp(qword[r14], 0);
    c.jne(output_back);
    c.mov(r14, views);
    c.vmovd(xmm0, eax);
    c.vmulss(xmm0, xmm0, dword[r14 + offsetof(FieldOfViews, scale)]);
    c.vmovss(dword[r15 + OutputFieldOfView], xmm0);
    c.vmovss(dword[r14 + offsetof(FieldOfViews, scaled)], xmm0);
    c.mov(dword[r14 + offsetof(FieldOfViews, own)], eax);
    c.L(output_back);
    c.jmp(reinterpret_cast<const void*>(game(Output.address + Output.size)));

    // A script's query of the view projects with the game's own field of view where the
    // parameters hold the scaled one.
    Xbyak::Label query, own, angle_query, box_projection, object_query, sphere_query;
    const void* projection = c.getCurr();
    c.push(rax);
    c.mov(rax, qword[rsp + 8]);
    c.cmp(rax, qword[rip + angle_query]);
    c.je(query);
    c.cmp(rax, qword[rip + box_projection]);
    c.jne(own);
    c.mov(rax, qword[rbp + 8]);
    c.cmp(rax, qword[rip + object_query]);
    c.je(query);
    c.cmp(rax, qword[rip + sphere_query]);
    c.jne(own);
    c.L(query);
    c.mov(rax, views);
    c.push(rcx);
    c.mov(ecx, dword[rsi]);
    c.cmp(ecx, dword[rax + offsetof(FieldOfViews, scaled)]);
    c.pop(rcx);
    c.jne(own);
    c.vbroadcastss(xmm3, dword[rax + offsetof(FieldOfViews, own)]);
    c.pop(rax);
    c.jmp(reinterpret_cast<const void*>(game(Projection.address + Projection.size)));
    c.L(own);
    c.pop(rax);
    c.vbroadcastss(xmm3, dword[rsi]);
    c.jmp(reinterpret_cast<const void*>(game(Projection.address + Projection.size)));
    c.L(angle_query);
    c.dq(game(AngleQueryReturn));
    c.L(box_projection);
    c.dq(game(BoxProjectionReturn));
    c.L(object_query);
    c.dq(game(ObjectQueryReturn));
    c.L(sphere_query);
    c.dq(game(SphereQueryReturn));

    if (Hooks::Replace(module_base, Output, c, capacity, output) != Hooks::Result::Hooked ||
        Hooks::Replace(module_base, Projection, c, capacity, projection) != Hooks::Result::Hooked) {
        LOG_ERROR(Render_Vulkan, "GR2port: the camera's field of view is not the expected code");
        return false;
    }
    return true;
}

void Set(u32 percent) {
    std::atomic_ref<float>(g_field_of_views.scale)
        .store(static_cast<float>(percent) / 100.0f, std::memory_order_relaxed);
}

} // namespace Gr2Port::FieldOfView
