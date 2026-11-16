// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

#include "common/types.h"
#include "graphics/gr2_fsr3/fsr4_context.h"
#include "graphics/renderer_vulkan/vk_common.h"

namespace Gr2Fsr3 {

/// XeSS quality settings, numbered as xess_quality_settings_t.
enum class XessQuality : u32 {
    UltraPerformance = 100,
    Balanced = 102,
    Quality = 103,
    UltraQuality = 104,
    Aa = 106,
};

/// One XeSS execution of the images of a context.
struct XessExecuteInfo {
    Fsr4Images images;
    vk::Extent2D render_size{};
    vk::Extent2D output_size{};
    float jitter_x{}; ///< Render pixels, as the viewport jitter.
    float jitter_y{};
    bool reset{};
};

#if defined(ENABLE_GR2_FSR3) && defined(_WIN32)

/// Loads libxess.dll from the xess folder of the user folder and appends the instance extensions
/// XeSS needs when it runs on Vulkan api_version.
void ExtendInstanceForXess(std::vector<const char*>& extensions, u32 api_version);

/// Appends the device extensions and features XeSS needs on physical_device to create_info.
void ExtendDeviceForXess(vk::Instance instance, vk::PhysicalDevice physical_device,
                         vk::DeviceCreateInfo& create_info);

/// Makes the XeSS context and starts building its pipelines. Returns why XeSS does not run, or an
/// empty string.
std::string XessCreate(vk::Instance instance, vk::PhysicalDevice physical_device,
                       vk::Device device);

/// Initializes XeSS for the sizes and scales the velocity by velocity_scale_x and
/// velocity_scale_y. Returns why it failed, or an empty string.
std::string XessInit(XessQuality quality, vk::Extent2D render_size, vk::Extent2D output_size,
                     float velocity_scale_x, float velocity_scale_y);

/// Records XeSS into cmdbuf. Returns why it failed, or an empty string.
std::string XessExecute(vk::CommandBuffer cmdbuf, const XessExecuteInfo& info);

/// Destroys the XeSS context.
void XessDestroy();

#else

inline void ExtendInstanceForXess(std::vector<const char*>&, u32) {}
inline void ExtendDeviceForXess(vk::Instance, vk::PhysicalDevice, vk::DeviceCreateInfo&) {}
inline std::string XessCreate(vk::Instance, vk::PhysicalDevice, vk::Device) {
    return "XeSS needs Windows";
}
inline std::string XessInit(XessQuality, vk::Extent2D, vk::Extent2D, float, float) {
    return "XeSS needs Windows";
}
inline std::string XessExecute(vk::CommandBuffer, const XessExecuteInfo&) {
    return "XeSS needs Windows";
}
inline void XessDestroy() {}

#endif

} // namespace Gr2Fsr3
