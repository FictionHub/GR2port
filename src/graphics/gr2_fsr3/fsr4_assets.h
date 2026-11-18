// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "common/types.h"
#include "graphics/renderer_vulkan/vk_common.h"

namespace Gr2Fsr3 {

/// Features the Vulkan device was created with that FSR 4 Lite shaders can need.
struct Fsr4DeviceFeatures {
    bool shader_float16{};
    bool shader_float64{};
    bool shader_int8{};
    bool shader_int16{};
    bool shader_int64{};
    bool shader_integer_dot_product{};
    bool subgroup_size_control{}; ///< Pipelines may require a subgroup size (32).
    bool storage_buffer_8bit{};
    bool uniform_storage_buffer_8bit{};
    bool storage_buffer_16bit{};
    bool uniform_storage_buffer_16bit{};
    bool image_gather_extended{};
    bool storage_image_extended_formats{};
    bool storage_image_read_without_format{};
    bool storage_image_write_without_format{};
    bool compute_shader_derivatives{}; ///< VK_KHR_compute_shader_derivatives is enabled.
    bool derivative_group_linear{};
    bool derivative_group_quads{};
};

/// What the device offers the files of a model set.
struct Fsr4DeviceLimits {
    Fsr4DeviceFeatures features{};
    u32 subgroup_operations{}; ///< VkSubgroupFeatureFlags supported in the compute stage.
    u64 max_storage_buffer_range{};
    u64 max_allocation_size{};
};

/// Shader modules of a model set: 0 pre-pass, 1-8 model passes, 9 post-pass.
constexpr std::size_t Fsr4ShaderCount = 10;

/// The files of one FSR 4 Lite model set, read and checked.
struct Fsr4Assets {
    std::array<std::vector<u32>, Fsr4ShaderCount> shaders;
    std::array<std::string, Fsr4ShaderCount> entry_points;
    std::vector<u8> initializer;
};

/// The assets, or the first problem as a notice; the summary is for the log.
struct Fsr4LoadResult {
    std::optional<Fsr4Assets> assets;
    std::string notice;
    std::string summary;
};

/**
 * Reads the model set of `preset` (GR2_FSR4_PRESET_*) for `output_size` from `folder` and checks
 * every file: present and complete, the model data sizes, SPIR-V with one GLCompute entry point,
 * capabilities and extensions the device enables, the Lite descriptor layout, and a scratch buffer
 * the device can bind. Any thread.
 */
Fsr4LoadResult LoadFsr4Assets(const std::filesystem::path& folder, u32 preset,
                              vk::Extent2D output_size, const Fsr4DeviceLimits& device);

} // namespace Gr2Fsr3
