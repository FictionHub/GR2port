// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>

#include "common/assert.h"
#include "common/uint128.h"
#include "core/libraries/gnmdriver/gnmdriver.h"
#include "core/libraries/kernel/time.h"
#include "core/memory.h"
#include "graphics/gr2port/gpu_writes.h"
#include "graphics/gr2port/hooks.h"

namespace Gr2Port::GpuWrites {

namespace {

// The sce::Gnm writers the executable links, each of which adds one packet to a command buffer.
// FUN_004164d0(buffer, event, destination, address, source, value, cache action, cache policy).
constexpr Hooks::Entry EndOfPipe{0x4164d0, 6, Hooks::FramePrologue};
// FUN_00416f50(buffer, event, address, value): a dword.
constexpr Hooks::Entry EndOfShader{0x416f50, 6, Hooks::FramePrologue};
// FUN_00412000(buffer, address, values, count, confirm): count dwords from values.
constexpr Hooks::Entry DataInline{0x412000, 6, Hooks::FramePrologue};
// FUN_00429910(buffer, event, destination, address, source, value, cache action, cache policy).
constexpr Hooks::Entry ReleaseMem{0x429910, 6, Hooks::FramePrologue};

// What a writer's source selector writes, which its packet holds as the data select.
enum class Source : u32 {
    LowDword = 1,
    Value = 2,
    GlobalClock = 3,
    CoreClock = 4,
};

// The GPU reads 40 bits of address.
constexpr u64 AddressMask = 0xff'ffff'ffffULL;

void Write(u64 address, const void* data, u64 size) {
    ASSERT(Core::Memory::Instance()->TryWriteBacking(reinterpret_cast<void*>(address), data, size));
}

// Writes the data source selects, with value as the immediate data.
void WriteSelected(u64 address, u32 source, u64 value) {
    u64 data = value;
    u64 size = sizeof(u64);
    switch (static_cast<Source>(source)) {
    case Source::LowDword:
        size = sizeof(u32);
        break;
    case Source::Value:
        break;
    case Source::GlobalClock:
        data = std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::high_resolution_clock::now().time_since_epoch())
                   .count();
        break;
    case Source::CoreClock:
        data = Common::MultiplyAndDivide64(Libraries::Kernel::sceKernelReadTsc(),
                                           Libraries::GnmDriver::sceGnmGetGpuCoreClockFrequency(),
                                           Libraries::Kernel::sceKernelGetTscFrequency());
        break;
    default:
        return;
    }
    Write(address & AddressMask & ~(size - 1), &data, size);
}

void PS4_SYSV_ABI OnEndOfPipe(u32, const Hooks::Registers* registers, u64) {
    WriteSelected(registers->rcx, static_cast<u32>(registers->r8), registers->r9);
}

void PS4_SYSV_ABI OnEndOfShader(u32, const Hooks::Registers* registers, u64) {
    const u32 value = static_cast<u32>(registers->rcx);
    Write(registers->rdx & AddressMask & ~u64{3}, &value, sizeof(value));
}

void PS4_SYSV_ABI OnDataInline(u32, const Hooks::Registers* registers, u64) {
    const u64 size = u64{static_cast<u32>(registers->rcx)} * sizeof(u32);
    if (size != 0) {
        Write(registers->rsi & ~u64{3}, reinterpret_cast<const void*>(registers->rdx), size);
    }
}

void PS4_SYSV_ABI OnReleaseMem(u32, const Hooks::Registers* registers, u64) {
    WriteSelected(registers->rcx, static_cast<u32>(registers->r8), registers->r9);
}

} // namespace

bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity) {
    if (!Hooks::Holds(module_base, EndOfPipe) || !Hooks::Holds(module_base, EndOfShader) ||
        !Hooks::Holds(module_base, DataInline) || !Hooks::Holds(module_base, ReleaseMem)) {
        return false;
    }
    return Hooks::Hooked(
               Hooks::ObserveEntry(module_base, EndOfPipe, c, capacity, &OnEndOfPipe, 0)) &&
           Hooks::Hooked(
               Hooks::ObserveEntry(module_base, EndOfShader, c, capacity, &OnEndOfShader, 0)) &&
           Hooks::Hooked(
               Hooks::ObserveEntry(module_base, DataInline, c, capacity, &OnDataInline, 0)) &&
           Hooks::Hooked(
               Hooks::ObserveEntry(module_base, ReleaseMem, c, capacity, &OnReleaseMem, 0));
}

} // namespace Gr2Port::GpuWrites
