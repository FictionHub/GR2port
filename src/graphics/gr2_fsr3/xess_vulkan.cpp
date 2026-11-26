// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <magic_enum/magic_enum.hpp>

#include "common/error.h"
#include "common/logging/log.h"
#include "common/path_util.h"
#include "graphics/gr2_fsr3/xess_vulkan.h"
#include "graphics/renderer_vulkan/vk_common.h"

// <vulkan/vulkan.h> must first be included through vk_common.h, which configures it.
#include <xess/xess_vk.h>

namespace Gr2Fsr3 {

namespace {

static_assert(static_cast<u32>(XessQuality::UltraPerformance) ==
              XESS_QUALITY_SETTING_ULTRA_PERFORMANCE);
static_assert(static_cast<u32>(XessQuality::Balanced) == XESS_QUALITY_SETTING_BALANCED);
static_assert(static_cast<u32>(XessQuality::Quality) == XESS_QUALITY_SETTING_QUALITY);
static_assert(static_cast<u32>(XessQuality::UltraQuality) == XESS_QUALITY_SETTING_ULTRA_QUALITY);
static_assert(static_cast<u32>(XessQuality::Aa) == XESS_QUALITY_SETTING_AA);

constexpr u32 Flags = XESS_INIT_FLAG_INVERTED_DEPTH | XESS_INIT_FLAG_LDR_INPUT_COLOR;

// Formats of the images XeSS reads and writes: colour, velocity, depth and output.
constexpr std::array<vk::Format, 4> ImageFormats{vk::Format::eR16G16B16A16Sfloat,
                                                 vk::Format::eR16G16Sfloat, vk::Format::eR32Sfloat,
                                                 vk::Format::eR16G16B16A16Sfloat};

// The import library is not linked: every function comes from GetProcAddress.
struct XessApi {
    decltype(&::xessVKGetRequiredInstanceExtensions) xessVKGetRequiredInstanceExtensions{};
    decltype(&::xessVKGetRequiredDeviceExtensions) xessVKGetRequiredDeviceExtensions{};
    decltype(&::xessVKGetRequiredDeviceFeatures) xessVKGetRequiredDeviceFeatures{};
    decltype(&::xessVKCreateContext) xessVKCreateContext{};
    decltype(&::xessVKBuildPipelines) xessVKBuildPipelines{};
    decltype(&::xessVKInit) xessVKInit{};
    decltype(&::xessVKExecute) xessVKExecute{};
    decltype(&::xessGetOptimalInputResolution) xessGetOptimalInputResolution{};
    decltype(&::xessSetVelocityScale) xessSetVelocityScale{};
    decltype(&::xessDestroyContext) xessDestroyContext{};
    decltype(&::xessGetVersion) xessGetVersion{};
    decltype(&::xessGetIntelXeFXVersion) xessGetIntelXeFXVersion{};
    decltype(&::xessSetLoggingCallback) xessSetLoggingCallback{};
};

enum class Stage : u8 { Off, Loaded, DeviceExtended };

XessApi g_api{};
Stage g_stage = Stage::Off;
std::string g_notice;
// The device create info points at these until vkCreateDevice returns.
std::vector<const char*> g_device_extensions;
xess_context_handle_t g_context = nullptr;

// The result's name, or its number when it has none.
std::string ResultText(xess_result_t result) {
    const std::string_view name = magic_enum::enum_name(result);
    return name.empty() ? std::to_string(static_cast<int>(result)) : std::string{name};
}

// Turns XeSS off with notice.
void Disable(std::string notice) {
    LOG_WARNING(Render_Vulkan, "GR2 XeSS: off: {}", notice);
    g_notice = std::move(notice);
    g_stage = Stage::Off;
}

// Logs XeSS's message at its level.
void ForwardLog(const char* message, xess_logging_level_t level) {
    if (level == XESS_LOGGING_LEVEL_ERROR) {
        LOG_ERROR(Render_Vulkan, "GR2 XeSS: {}", message);
    } else {
        LOG_WARNING(Render_Vulkan, "GR2 XeSS: {}", message);
    }
}

// Returns the first export the library lacks, or an empty string.
std::string ResolveExports(HMODULE module) {
    std::string missing;
    const auto resolve = [&](const char* name, auto& function) {
        function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(
            GetProcAddress(module, name));
        if (!function && missing.empty()) {
            missing = name;
        }
    };
    resolve("xessVKGetRequiredInstanceExtensions", g_api.xessVKGetRequiredInstanceExtensions);
    resolve("xessVKGetRequiredDeviceExtensions", g_api.xessVKGetRequiredDeviceExtensions);
    resolve("xessVKGetRequiredDeviceFeatures", g_api.xessVKGetRequiredDeviceFeatures);
    resolve("xessVKCreateContext", g_api.xessVKCreateContext);
    resolve("xessVKBuildPipelines", g_api.xessVKBuildPipelines);
    resolve("xessVKInit", g_api.xessVKInit);
    resolve("xessVKExecute", g_api.xessVKExecute);
    resolve("xessGetOptimalInputResolution", g_api.xessGetOptimalInputResolution);
    resolve("xessSetVelocityScale", g_api.xessSetVelocityScale);
    resolve("xessDestroyContext", g_api.xessDestroyContext);
    resolve("xessGetVersion", g_api.xessGetVersion);
    resolve("xessGetIntelXeFXVersion", g_api.xessGetIntelXeFXVersion);
    resolve("xessSetLoggingCallback", g_api.xessSetLoggingCallback);
    return missing;
}

// Appends the names the list lacks.
void AppendMissing(std::vector<const char*>& list, std::span<const char* const> names) {
    for (const char* name : names) {
        const bool present = std::ranges::any_of(
            list, [name](const char* entry) { return std::strcmp(entry, name) == 0; });
        if (!present) {
            list.push_back(name);
        }
    }
}

// XeSS's description of image at extent.
xess_vk_image_view_info ViewOf(const Fsr4Image& image, vk::Format format, vk::Extent2D extent) {
    return {
        .imageView = static_cast<VkImageView>(image.view),
        .image = static_cast<VkImage>(image.image),
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        .format = static_cast<VkFormat>(format),
        .width = extent.width,
        .height = extent.height,
    };
}

} // namespace

void ExtendInstanceForXess(std::vector<const char*>& extensions, u32 api_version) {
    const std::filesystem::path library =
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "xess" / "libxess.dll";
    const std::string library_text = Common::FS::PathToUTF8String(library);
    if (!std::filesystem::exists(library)) {
        Disable(fmt::format("XeSS needs {}", library_text));
        return;
    }
    const HMODULE module =
        LoadLibraryExW(library.c_str(), nullptr,
                       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) {
        Disable(fmt::format("XeSS: libxess.dll did not load: {}", Common::GetLastErrorMsg()));
        return;
    }
    if (const std::string name = ResolveExports(module); !name.empty()) {
        Disable(fmt::format("XeSS: libxess.dll lacks {}", name));
        return;
    }
    u32 count = 0;
    const char* const* names = nullptr;
    u32 min_version = 0;
    if (const xess_result_t result =
            g_api.xessVKGetRequiredInstanceExtensions(&count, &names, &min_version);
        result != XESS_RESULT_SUCCESS) {
        Disable(fmt::format("XeSS gave no instance extensions: {}", ResultText(result)));
        return;
    }
    if (min_version > api_version) {
        Disable(fmt::format("XeSS needs Vulkan {}.{}", VK_API_VERSION_MAJOR(min_version),
                            VK_API_VERSION_MINOR(min_version)));
        return;
    }
    const std::span<const char* const> required{names, count};
    AppendMissing(extensions, required);
    LOG_INFO(Render_Vulkan, "GR2 XeSS: loaded {}; instance extensions: {}", library_text,
             fmt::join(required, ", "));
    g_stage = Stage::Loaded;
}

void ExtendDeviceForXess(vk::Instance instance, vk::PhysicalDevice physical_device,
                         vk::DeviceCreateInfo& create_info) {
    if (g_stage != Stage::Loaded) {
        return;
    }
    const auto vk_instance = static_cast<VkInstance>(instance);
    const auto gpu = static_cast<VkPhysicalDevice>(physical_device);
    u32 count = 0;
    const char* const* names = nullptr;
    if (const xess_result_t result =
            g_api.xessVKGetRequiredDeviceExtensions(vk_instance, gpu, &count, &names);
        result != XESS_RESULT_SUCCESS) {
        Disable(fmt::format("XeSS gave no device extensions: {}", ResultText(result)));
        return;
    }
    void* features = const_cast<void*>(create_info.pNext);
    if (const xess_result_t result =
            g_api.xessVKGetRequiredDeviceFeatures(vk_instance, gpu, &features);
        result != XESS_RESULT_SUCCESS) {
        Disable(fmt::format("XeSS gave no device features: {}", ResultText(result)));
        return;
    }
    create_info.pNext = features;
    const std::span<const char* const> required{names, count};
    g_device_extensions.assign(create_info.ppEnabledExtensionNames,
                               create_info.ppEnabledExtensionNames +
                                   create_info.enabledExtensionCount);
    AppendMissing(g_device_extensions, required);
    create_info.enabledExtensionCount = static_cast<u32>(g_device_extensions.size());
    create_info.ppEnabledExtensionNames = g_device_extensions.data();
    LOG_INFO(Render_Vulkan, "GR2 XeSS: device extensions: {}", fmt::join(required, ", "));
    g_stage = Stage::DeviceExtended;
}

std::string XessCreate(vk::Instance instance, vk::PhysicalDevice physical_device, vk::Device device,
                       vk::PipelineCache pipeline_cache) {
    if (g_stage != Stage::DeviceExtended) {
        return g_notice;
    }
    xess_context_handle_t context = nullptr;
    if (const xess_result_t result = g_api.xessVKCreateContext(
            static_cast<VkInstance>(instance), static_cast<VkPhysicalDevice>(physical_device),
            static_cast<VkDevice>(device), &context);
        result != XESS_RESULT_SUCCESS) {
        return fmt::format("the XeSS context was not made: {}", ResultText(result));
    }
    g_api.xessSetLoggingCallback(context, XESS_LOGGING_LEVEL_WARNING, ForwardLog);
    if (const xess_result_t result = g_api.xessVKBuildPipelines(
            context, static_cast<VkPipelineCache>(pipeline_cache), true, Flags);
        result != XESS_RESULT_SUCCESS) {
        g_api.xessDestroyContext(context);
        return fmt::format("XeSS did not build its pipelines: {}", ResultText(result));
    }
    g_context = context;
    xess_version_t version{};
    xess_version_t xefx{};
    g_api.xessGetVersion(&version);
    g_api.xessGetIntelXeFXVersion(g_context, &xefx);
    LOG_INFO(Render_Vulkan, "GR2 XeSS: XeSS {}.{}.{}, XeFX {}.{}.{}", version.major, version.minor,
             version.patch, xefx.major, xefx.minor, xefx.patch);
    return {};
}

std::string XessInit(XessQuality quality, vk::Extent2D render_size, vk::Extent2D output_size,
                     float velocity_scale_x, float velocity_scale_y) {
    const auto setting = static_cast<xess_quality_settings_t>(quality);
    const xess_2d_t output{output_size.width, output_size.height};
    xess_2d_t optimal{};
    xess_2d_t min{};
    xess_2d_t max{};
    if (const xess_result_t result =
            g_api.xessGetOptimalInputResolution(g_context, &output, setting, &optimal, &min, &max);
        result != XESS_RESULT_SUCCESS) {
        return fmt::format("XeSS has no input sizes for {}x{}: {}", output.x, output.y,
                           ResultText(result));
    }
    if (render_size.width < min.x || render_size.height < min.y || render_size.width > max.x ||
        render_size.height > max.y) {
        return fmt::format("XeSS quality {} takes {}x{} to {}x{} for {}x{}, not {}x{}",
                           static_cast<u32>(quality), min.x, min.y, max.x, max.y, output.x,
                           output.y, render_size.width, render_size.height);
    }
    const xess_vk_init_params_t params{
        .outputResolution = output,
        .qualitySetting = setting,
        .initFlags = Flags,
    };
    if (const xess_result_t result = g_api.xessVKInit(g_context, &params);
        result != XESS_RESULT_SUCCESS) {
        return fmt::format("XeSS did not initialize for {}x{}: {}", output.x, output.y,
                           ResultText(result));
    }
    if (const xess_result_t result =
            g_api.xessSetVelocityScale(g_context, velocity_scale_x, velocity_scale_y);
        result != XESS_RESULT_SUCCESS) {
        return fmt::format("XeSS rejected the velocity scale: {}", ResultText(result));
    }
    return {};
}

std::string XessExecute(vk::CommandBuffer cmdbuf, const XessExecuteInfo& info) {
    const Fsr4Images& images = info.images;
    const xess_vk_execute_params_t params{
        .colorTexture = ViewOf(images.color, ImageFormats[0], info.render_size),
        .velocityTexture = ViewOf(images.motion_vectors, ImageFormats[1], info.render_size),
        .depthTexture = ViewOf(images.depth, ImageFormats[2], info.render_size),
        .outputTexture = ViewOf(images.output, ImageFormats[3], info.output_size),
        .jitterOffsetX = info.jitter_x,
        .jitterOffsetY = info.jitter_y,
        .exposureScale = 1.0f,
        .resetHistory = info.reset ? 1u : 0u,
        .inputWidth = info.render_size.width,
        .inputHeight = info.render_size.height,
    };
    if (const xess_result_t result =
            g_api.xessVKExecute(g_context, static_cast<VkCommandBuffer>(cmdbuf), &params);
        result != XESS_RESULT_SUCCESS) {
        return fmt::format("XeSS rejected the frame: {}", ResultText(result));
    }
    return {};
}

void XessDestroy() {
    if (g_context != nullptr) {
        g_api.xessDestroyContext(g_context);
        g_context = nullptr;
    }
}

} // namespace Gr2Fsr3
