// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <xbyak/xbyak.h>

#include "common/logging/log.h"
#include "graphics/gr2port/hooks.h"
#include "graphics/gr2port/marker_fit.h"

namespace Gr2Port::MarkerFit {

namespace {

// The vectors of 0.01 that turn a marker's offset in the canvas into its layout's offset. Each
// belongs to one marker's update: FUN_0172fae0, FUN_01736c50, FUN_0173ebe0, FUN_0175afc0,
// FUN_017614f0, FUN_01764280, FUN_01766900 and FUN_0176a810, in this order.
constexpr std::array<VAddr, 8> OffsetScales{
    0x18972d0, 0x1897430, 0x1897650, 0x1897f10, 0x1898160, 0x1898230, 0x1898310, 0x1898410,
};
constexpr float OffsetScale = 0.01f;

// FUN_013e0580 places a layout by the matrix in rsi; FUN_0150c110 draws a string by the matrix
// in rdx. Both matrices already include the screen drawing's projection.
constexpr VAddr LayoutUpdate = 0x13e0580;
constexpr VAddr TextDraw = 0x150c110;

constexpr Hooks::Entry CallOf(VAddr site, VAddr target) {
    const auto displacement = static_cast<u32>(target - (site + 5));
    return {site,
            5,
            {0xe8, static_cast<u8>(displacement), static_cast<u8>(displacement >> 8),
             static_cast<u8>(displacement >> 16), static_cast<u8>(displacement >> 24)}};
}

// The calls that place the layouts positioned by the offsets above.
constexpr std::array LayoutCalls{
    CallOf(0x17303ea, LayoutUpdate), CallOf(0x173734f, LayoutUpdate),
    CallOf(0x173f256, LayoutUpdate), CallOf(0x175e13b, LayoutUpdate),
    CallOf(0x17621c1, LayoutUpdate), CallOf(0x1764e30, LayoutUpdate),
    CallOf(0x1764f97, LayoutUpdate), CallOf(0x17651d8, LayoutUpdate),
    CallOf(0x17651e4, LayoutUpdate), CallOf(0x17660ef, LayoutUpdate),
    CallOf(0x1767b46, LayoutUpdate), CallOf(0x176ba10, LayoutUpdate),
};

// FUN_01764280's calls that draw a marker's name plate.
constexpr std::array TextCalls{
    CallOf(0x17656c8, TextDraw),
    CallOf(0x1765c77, TextDraw),
    CallOf(0x1766009, TextDraw),
};

// FUN_01764280 places the name plate from the marker's canvas position instead of its offset:
// vaddps xmm0, xmm0, [PlateOrigin] gives its x from the middle and its y from 40 above the middle.
// The instruction after it branches on flags the stub leaves alone.
constexpr Hooks::Entry PlatePlace{0x1764d1c, 8, {0xc5, 0xf8, 0x58, 0x05, 0x5c, 0x35, 0x13, 0x00}};
constexpr VAddr PlateOrigin = 0x1898280;
constexpr VAddr PlateNext = 0x1764d24;
constexpr float PlateLift = 40.0f;

// FUN_013ebe60 returns whether a layout element's bounds lie past one edge of the canvas, as a
// marker's do beside the 16:9 middle, and leaves the answer in the element. It takes the element's
// matrix in rsi and the element in rcx.
constexpr Hooks::Entry BoundsTest{0x13ebe60, 6, {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57}};
constexpr VAddr BoundsTestNext = 0x13ebe66;
constexpr u64 ElementOutside = 0xc;

constexpr size_t StubBytes = 168;

VAddr g_module_base = 0;
float* g_plate_gain = nullptr;
std::array<float, 2> g_fit_scale{1.0f, 1.0f};

void WriteTag(Xbyak::CodeGenerator& c, const Xbyak::Reg64& matrix) {
    using namespace Xbyak::util;
    c.mov(dword[matrix + 2 * sizeof(float)], 0);
    c.mov(dword[matrix + 6 * sizeof(float)], 0);
    c.mov(dword[matrix + 10 * sizeof(float)], 0);
    c.mov(dword[matrix + 14 * sizeof(float)], std::bit_cast<u32>(DrawTag));
}

void WriteVector(Xbyak::CodeGenerator& c, const std::array<float, 4>& vector) {
    for (const float value : vector) {
        c.dd(std::bit_cast<u32>(value));
    }
}

} // namespace

bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity) {
    using namespace Xbyak::util;
    const auto holds = [module_base](const Hooks::Entry& entry) {
        return Hooks::Holds(module_base, entry);
    };
    if (!std::ranges::all_of(LayoutCalls, holds) || !std::ranges::all_of(TextCalls, holds) ||
        !holds(PlatePlace) || !holds(BoundsTest)) {
        LOG_ERROR(Render_Vulkan, "GR2port: the markers' code is not the expected code");
        return false;
    }
    const size_t hooks = LayoutCalls.size() + TextCalls.size() + 2;
    if (capacity - c.getSize() < StubBytes + hooks * Hooks::MaxStubBytes) {
        LOG_ERROR(Render_Vulkan, "GR2port: no room for the markers' hooks");
        return false;
    }
    const auto code = [module_base](VAddr address) { return Hooks::Code(module_base, address); };

    const u8* const lift = c.getCurr();
    WriteVector(c, {0.0f, PlateLift, 0.0f, 0.0f});
    const u8* const gain = c.getCurr();
    WriteVector(c, {1.0f, 1.0f, 1.0f, 1.0f});

    const void* layout_call = c.getCurr();
    WriteTag(c, rsi);
    c.jmp(code(LayoutUpdate));

    const void* text_call = c.getCurr();
    WriteTag(c, rdx);
    c.jmp(code(TextDraw));

    const void* plate = c.getCurr();
    c.vaddps(xmm0, xmm0, ptr[rip + code(PlateOrigin)]);
    c.vsubps(xmm0, xmm0, ptr[rip + lift]);
    c.vmulps(xmm0, xmm0, ptr[rip + gain]);
    c.vaddps(xmm0, xmm0, ptr[rip + lift]);
    c.jmp(code(PlateNext));

    const void* bounds_test = c.getCurr();
    Xbyak::Label other_element;
    c.cmp(dword[rsi + 14 * sizeof(float)], std::bit_cast<u32>(DrawTag));
    c.jne(other_element);
    c.mov(byte[rcx + ElementOutside], 0);
    c.xor_(eax, eax);
    c.ret();
    c.L(other_element);
    c.push(rbp);
    c.mov(rbp, rsp);
    c.push(r15);
    c.jmp(code(BoundsTestNext));

    const auto hook_calls = [&](const auto& calls, const void* thunk) {
        for (const Hooks::Entry& entry : calls) {
            const void* stub = c.getCurr();
            c.call(thunk);
            c.jmp(code(entry.address + entry.size));
            if (Hooks::Replace(module_base, entry, c, capacity, stub) != Hooks::Result::Hooked) {
                return false;
            }
        }
        return true;
    };
    if (!hook_calls(LayoutCalls, layout_call) || !hook_calls(TextCalls, text_call) ||
        Hooks::Replace(module_base, PlatePlace, c, capacity, plate) != Hooks::Result::Hooked ||
        Hooks::Replace(module_base, BoundsTest, c, capacity, bounds_test) !=
            Hooks::Result::Hooked) {
        LOG_ERROR(Render_Vulkan, "GR2port: the markers' hooks did not go in");
        return false;
    }
    g_plate_gain = reinterpret_cast<float*>(const_cast<u8*>(gain));
    g_module_base = module_base;
    return true;
}

void SetFitScale(std::array<float, 2> fit_scale) {
    if (g_module_base == 0 || fit_scale == g_fit_scale) {
        return;
    }
    g_fit_scale = fit_scale;
    const float x = 1.0f / fit_scale[0];
    const float y = 1.0f / fit_scale[1];
    const std::array scale{OffsetScale * x, OffsetScale * y, OffsetScale, OffsetScale};
    for (const VAddr cell : OffsetScales) {
        std::memcpy(const_cast<u8*>(Hooks::Code(g_module_base, cell)), scale.data(), sizeof(scale));
    }
    g_plate_gain[0] = x;
    g_plate_gain[1] = y;
}

} // namespace Gr2Port::MarkerFit
