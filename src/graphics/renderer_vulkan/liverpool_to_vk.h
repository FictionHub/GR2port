// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include "common/assert.h"
#include "graphics/amdgpu/pixel_format.h"
#include "graphics/renderer_vulkan/vk_common.h"

namespace Vulkan::LiverpoolToVK {

[[noreturn]] void ComponentSwizzleInvalid(u32 raw);

/// Table lookup with the crash path outlined: this was the single heaviest
/// call edge in the binary (four calls per image view), and the UNREACHABLE
/// machinery in the switch kept it out of line. CompSwizzle values 2 and 3
/// are reserved and must keep crashing, not silently map.
inline vk::ComponentSwizzle ComponentSwizzle(AmdGpu::CompSwizzle comp_swizzle) {
    static constexpr std::array<vk::ComponentSwizzle, 8> table = {
        vk::ComponentSwizzle::eZero, // Zero = 0
        vk::ComponentSwizzle::eOne,  // One = 1
        vk::ComponentSwizzle::eZero, // reserved (crashes below)
        vk::ComponentSwizzle::eZero, // reserved (crashes below)
        vk::ComponentSwizzle::eR,    // Red = 4
        vk::ComponentSwizzle::eG,    // Green = 5
        vk::ComponentSwizzle::eB,    // Blue = 6
        vk::ComponentSwizzle::eA,    // Alpha = 7
    };
    const u32 raw = static_cast<u32>(comp_swizzle);
    if (raw >= table.size() || raw == 2 || raw == 3) [[unlikely]] {
        ComponentSwizzleInvalid(raw);
    }
    return table[raw];
}

inline vk::ComponentMapping ComponentMapping(AmdGpu::CompMapping comp_mapping) {
    return vk::ComponentMapping{
        .r = ComponentSwizzle(comp_mapping.r),
        .g = ComponentSwizzle(comp_mapping.g),
        .b = ComponentSwizzle(comp_mapping.b),
        .a = ComponentSwizzle(comp_mapping.a),
    };
}

struct SurfaceFormatInfo {
    AmdGpu::DataFormat data_format;
    AmdGpu::NumberFormat number_format;
    vk::Format vk_format;
};

// Table 8.13 Data and Image Formats [Sea Islands Series Instruction Set Architecture]
constexpr size_t amd_gpu_data_format_bit_size = 6;   // All values are under 64
constexpr size_t amd_gpu_number_format_bit_size = 4; // All values are under 16
constexpr size_t surface_format_table_size =
    (1u << amd_gpu_data_format_bit_size) * (1u << amd_gpu_number_format_bit_size);

extern const std::array<vk::Format, surface_format_table_size> surface_format_table;

constexpr size_t GetSurfaceFormatTableIndex(AmdGpu::DataFormat data_format,
                                            AmdGpu::NumberFormat num_format) {
    DEBUG_ASSERT(u32(data_format) < 1 << amd_gpu_data_format_bit_size);
    DEBUG_ASSERT(u32(num_format) < 1 << amd_gpu_number_format_bit_size);
    size_t result = static_cast<size_t>(num_format) |
                    (static_cast<size_t>(data_format) << amd_gpu_number_format_bit_size);
    return result;
}

} // namespace Vulkan::LiverpoolToVK
