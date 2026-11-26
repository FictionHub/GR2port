// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <magic_enum/magic_enum.hpp>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <sys/mman.h>
#endif

#include "common/alignment.h"
#include "common/logging/log.h"
#include "common/path_util.h"
#include "graphics/gr2_fsr3/dlss_ngx.h"
#include "graphics/gr2port/settings.h"
#include "graphics/renderer_vulkan/vk_common.h"

// <vulkan/vulkan.h> must first be included through vk_common.h, which configures it.
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_vk.h>

namespace Gr2Fsr3 {

namespace {

using Clock = std::chrono::steady_clock;

static_assert(static_cast<u32>(DlssMode::Performance) == NVSDK_NGX_PerfQuality_Value_MaxPerf);
static_assert(static_cast<u32>(DlssMode::Balanced) == NVSDK_NGX_PerfQuality_Value_Balanced);
static_assert(static_cast<u32>(DlssMode::Quality) == NVSDK_NGX_PerfQuality_Value_MaxQuality);
static_assert(static_cast<u32>(DlssMode::UltraPerformance) ==
              NVSDK_NGX_PerfQuality_Value_UltraPerformance);
static_assert(static_cast<u32>(DlssMode::Dlaa) == NVSDK_NGX_PerfQuality_Value_DLAA);

// The DLSS Super Resolution library of the DLSS SDK 310.9.16, from lib/<platform>/rel of the SDK.
#ifdef _WIN32
constexpr std::string_view DlssLibrary = "nvngx_dlss.dll";
#else
constexpr std::string_view DlssLibrary = "libnvidia-ngx-dlss.so.310.9.16";
#endif

// With an engine version and a project ID, NGX needs no application ID from NVIDIA.
constexpr const char* EngineVersion = "GR2fork-EXODUS";
constexpr const char* ProjectId = "23459be6-f898-4aad-9dbc-18fbdd716555";

// cuInit reserves address space below 1 TiB, and the Gravity Rush games map their memory below
// 64 GiB. The free space below this address is held while cuInit runs.
constexpr uintptr_t CudaLowestAddress = 512_GB;

// Indexed by DlssPreset.
constexpr std::array<u32, 6> Presets{
    NVSDK_NGX_DLSS_Hint_Render_Preset_Default, NVSDK_NGX_DLSS_Hint_Render_Preset_E,
    NVSDK_NGX_DLSS_Hint_Render_Preset_J,       NVSDK_NGX_DLSS_Hint_Render_Preset_K,
    NVSDK_NGX_DLSS_Hint_Render_Preset_L,       NVSDK_NGX_DLSS_Hint_Render_Preset_M};
static_assert(Presets.size() == magic_enum::enum_count<DlssPreset>());

// The motion vectors are at the render size and the depth is reversed: nearer is greater. DLSS
// computes the exposure of the LDR colour itself.
constexpr int FeatureFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
                             NVSDK_NGX_DLSS_Feature_Flags_DepthInverted |
                             NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;

// Each boot step runs only after the previous one succeeded.
enum class Stage : u8 { Off, InstanceExtended, DeviceExtended, Running };

// The dlss folder: where NGX finds the DLSS library and keeps its data.
std::wstring g_folder;
std::array<const wchar_t*, 1> g_folder_list{};
NVSDK_NGX_FeatureCommonInfo g_common{};
// The instance and device create infos point at these until their create calls return.
std::vector<std::string> g_instance_extensions;
std::vector<std::string> g_device_extension_names;
std::vector<const char*> g_device_extensions;
vk::Device g_device;
// NGX's capability parameters, which the feature calls take too.
NVSDK_NGX_Parameter* g_params{};
NVSDK_NGX_Handle* g_feature{};
DlssMode g_mode{DlssMode::Quality};
DlssPreset g_preset{DlssPreset::Default};
#ifndef _WIN32
// Written on the main thread before the renderer starts.
bool g_cuda_started = false;
#endif
// Written on the main thread before the stage is stored.
std::string g_notice;
std::atomic<Stage> g_stage{Stage::Off};

// The result's code and, as NGX names it in ASCII, its name.
std::string ResultText(NVSDK_NGX_Result result) {
    std::string text = fmt::format("0x{:08X}", static_cast<u32>(result));
    if (const wchar_t* name = GetNGXResultAsString(result); name != nullptr) {
        text.append(" (");
        for (const wchar_t* c = name; *c != L'\0'; ++c) {
            text.push_back(static_cast<char>(*c));
        }
        text.push_back(')');
    }
    return text;
}

void Disable(std::string notice) {
    LOG_WARNING(Render_Vulkan, "GR2 DLSS: off: {}", notice);
    g_notice = std::move(notice);
    g_stage.store(Stage::Off, std::memory_order_release);
}

// Whether GR2port's graphics settings choose DLSS.
bool DlssRequested() {
    return Gr2Port::Settings::Get().graphics.upscaler == Gr2Port::Settings::Upscaler::Dlss;
}

// Formats of the images DLSS reads and writes: colour, motion vectors, depth and output.
constexpr std::array<vk::Format, 4> ImageFormats{vk::Format::eR16G16B16A16Sfloat,
                                                 vk::Format::eR16G16Sfloat, vk::Format::eR32Sfloat,
                                                 vk::Format::eR16G16B16A16Sfloat};

// Drops the line breaks that end NGX's lines.
void NVSDK_CONV ForwardLog(const char* message, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature) {
    std::string_view text{message};
    while (text.ends_with('\n') || text.ends_with('\r')) {
        text.remove_suffix(1);
    }
    LOG_INFO(Render_Vulkan, "GR2 DLSS: NGX: {}", text);
}

NVSDK_NGX_FeatureDiscoveryInfo DiscoveryInfo() {
    NVSDK_NGX_FeatureDiscoveryInfo info{};
    info.SDKVersion = NVSDK_NGX_Version_API;
    info.FeatureID = NVSDK_NGX_Feature_SuperSampling;
    info.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
    info.Identifier.v.ProjectDesc.ProjectId = ProjectId;
    info.Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
    info.Identifier.v.ProjectDesc.EngineVersion = EngineVersion;
    info.ApplicationDataPath = g_folder.c_str();
    info.FeatureInfo = &g_common;
    return info;
}

// The reasons a support result gives, comma-separated.
std::string UnsupportedText(NVSDK_NGX_Feature_Support_Result result) {
    constexpr std::array<std::pair<u32, std::string_view>, 5> Reasons{{
        {NVSDK_NGX_FeatureSupportResult_CheckNotPresent, "the driver has no support check"},
        {NVSDK_NGX_FeatureSupportResult_DriverVersionUnsupported, "the driver is too old"},
        {NVSDK_NGX_FeatureSupportResult_AdapterUnsupported, "the GPU has no DLSS"},
        {NVSDK_NGX_FeatureSupportResult_OSVersionBelowMinimumSupported, "the OS is too old"},
        {NVSDK_NGX_FeatureSupportResult_NotImplemented, "NGX has no DLSS here"},
    }};
    std::vector<std::string_view> reasons;
    for (const auto& [bit, reason] : Reasons) {
        if ((static_cast<u32>(result) & bit) != 0) {
            reasons.push_back(reason);
        }
    }
    if (reasons.empty()) {
        return fmt::format("support result 0x{:X}", static_cast<u32>(result));
    }
    return fmt::format("{}", fmt::join(reasons, ", "));
}

// The names of the extensions, which outlive the properties NGX returns.
std::vector<std::string> NamesOf(std::span<const VkExtensionProperties> properties) {
    std::vector<std::string> names;
    for (const VkExtensionProperties& property : properties) {
        names.emplace_back(property.extensionName);
    }
    return names;
}

// The names the properties do not list, comma-separated.
template <typename Properties>
std::string Unavailable(std::span<const std::string> names, const Properties& properties) {
    std::string missing;
    for (const std::string& name : names) {
        const bool listed = std::ranges::any_of(properties, [&name](const auto& property) {
            return name == property.extensionName.data();
        });
        if (!listed) {
            missing.append(missing.empty() ? "" : ", ").append(name);
        }
    }
    return missing;
}

// Appends the names the list lacks.
void AppendMissing(std::vector<const char*>& list, std::span<const std::string> names) {
    for (const std::string& name : names) {
        const bool present =
            std::ranges::any_of(list, [&name](const char* entry) { return name == entry; });
        if (!present) {
            list.push_back(name.c_str());
        }
    }
}

// Sets the preset of every mode.
void SetPresets(DlssPreset preset) {
    const u32 value = Presets[static_cast<std::size_t>(preset)];
    for (const char* name : {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,
                             NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
                             NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,
                             NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
                             NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance}) {
        NVSDK_NGX_Parameter_SetUI(g_params, name, value);
    }
}

NVSDK_NGX_Resource_VK ResourceOf(const Fsr4Image& image, vk::Format format, vk::Extent2D extent,
                                 bool written) {
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return NVSDK_NGX_Create_ImageView_Resource_VK(
        static_cast<VkImageView>(image.view), static_cast<VkImage>(image.image), range,
        static_cast<VkFormat>(format), extent.width, extent.height, written);
}

} // namespace

const char* DlssModeName(DlssMode mode) {
    switch (mode) {
    case DlssMode::Performance:
        return "Performance";
    case DlssMode::Balanced:
        return "Balanced";
    case DlssMode::Quality:
        return "Quality";
    case DlssMode::UltraPerformance:
        return "Ultra Performance";
    case DlssMode::Dlaa:
        return "DLAA";
    }
    return "";
}

void StartCudaForDlss() {
#ifdef _WIN32
    if (!DlssRequested()) {
        return;
    }
    const HMODULE module = LoadLibraryW(L"nvcuda.dll");
    const auto cu_init =
        module ? reinterpret_cast<int (*)(unsigned int)>(GetProcAddress(module, "cuInit"))
               : nullptr;
    if (!cu_init) {
        return;
    }
    std::vector<void*> held;
    MEMORY_BASIC_INFORMATION info{};
    for (uintptr_t address = 64_KB; address < CudaLowestAddress;
         address = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize) {
        VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info));
        const auto base = reinterpret_cast<uintptr_t>(info.BaseAddress);
        const uintptr_t start = Common::AlignUp(base, 64_KB);
        const uintptr_t end = std::min(base + info.RegionSize, CudaLowestAddress);
        if (info.State != MEM_FREE || start >= end) {
            continue;
        }
        if (void* reserved = VirtualAlloc(reinterpret_cast<void*>(start), end - start, MEM_RESERVE,
                                          PAGE_NOACCESS)) {
            held.push_back(reserved);
        }
    }
    const int result = cu_init(0);
    for (void* reserved : held) {
        VirtualFree(reserved, 0, MEM_RELEASE);
    }
    if (result == 0) {
        LOG_INFO(Render_Vulkan, "GR2 DLSS: CUDA started");
    } else {
        LOG_WARNING(Render_Vulkan, "GR2 DLSS: CUDA did not start: error {}", result);
    }
#else
    if (!DlssRequested()) {
        return;
    }
    // libcuda stays loaded: the device that NGX's extensions make uses the CUDA started here.
    void* const library = dlopen("libcuda.so.1", RTLD_NOW);
    const auto cu_init =
        library ? reinterpret_cast<int (*)(unsigned int)>(dlsym(library, "cuInit")) : nullptr;
    if (!cu_init) {
        LOG_WARNING(Render_Vulkan, "GR2 DLSS: CUDA did not start: no libcuda.so.1");
        return;
    }
    void* const lowest = reinterpret_cast<void*>(64_KB);
    const std::size_t held_size = CudaLowestAddress - 64_KB;
    void* const held =
        mmap(lowest, held_size, PROT_NONE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (held != lowest) {
        if (held != MAP_FAILED) {
            munmap(held, held_size);
        }
        LOG_WARNING(Render_Vulkan,
                    "GR2 DLSS: CUDA did not start: the space below 512 GiB is in use");
        return;
    }
    const int result = cu_init(0);
    munmap(held, held_size);
    if (result == 0) {
        g_cuda_started = true;
        LOG_INFO(Render_Vulkan, "GR2 DLSS: CUDA started");
    } else {
        LOG_WARNING(Render_Vulkan, "GR2 DLSS: CUDA did not start: error {}", result);
    }
#endif
}

void ExtendInstance(std::vector<const char*>& extensions,
                    const vk::detail::DispatchLoaderDynamic& dispatch) {
    if (!DlssRequested()) {
        return;
    }
    const std::filesystem::path folder = DlssDir();
    const std::string folder_text = Common::FS::PathToUTF8String(folder);
    if (!std::filesystem::exists(folder / DlssLibrary)) {
        Disable(fmt::format("DLSS needs {} in {}", DlssLibrary, folder_text));
        return;
    }
#ifndef _WIN32
    // NGX's device extensions start CUDA in the device, where the guest's space leaves it no room.
    if (!g_cuda_started) {
        Disable("DLSS needs CUDA, which did not start before the game");
        return;
    }
#endif
    g_folder = folder.wstring();
    g_folder_list[0] = g_folder.c_str();
    g_common.PathListInfo.Path = g_folder_list.data();
    g_common.PathListInfo.Length = static_cast<u32>(g_folder_list.size());
    // NGX's lines go to GR2port's log only, in time with the renderer's.
    g_common.LoggingInfo.LoggingCallback = ForwardLog;
    g_common.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
    g_common.LoggingInfo.DisableOtherLoggingSinks = true;

    const NVSDK_NGX_FeatureDiscoveryInfo discovery = DiscoveryInfo();
    u32 count = 0;
    VkExtensionProperties* required{};
    if (const NVSDK_NGX_Result result =
            NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&discovery, &count, &required);
        NVSDK_NGX_FAILED(result)) {
        Disable(fmt::format("DLSS: NGX gave no instance extensions: {}", ResultText(result)));
        return;
    }
    g_instance_extensions = NamesOf({required, count});
    const auto [properties_result, properties] =
        vk::enumerateInstanceExtensionProperties(nullptr, dispatch);
    if (const std::string lacking = Unavailable(g_instance_extensions, properties);
        !lacking.empty()) {
        Disable(fmt::format("DLSS: the Vulkan instance lacks {}", lacking));
        return;
    }
    AppendMissing(extensions, g_instance_extensions);
    LOG_INFO(Render_Vulkan, "GR2 DLSS: {} in {}; instance extensions: {}", DlssLibrary, folder_text,
             fmt::join(g_instance_extensions, ", "));
    g_stage.store(Stage::InstanceExtended, std::memory_order_release);
}

void ExtendDeviceForDlss(vk::Instance instance, vk::PhysicalDevice physical_device,
                         vk::DeviceCreateInfo& create_info,
                         const vk::detail::DispatchLoaderDynamic& dispatch) {
    if (g_stage.load(std::memory_order_acquire) != Stage::InstanceExtended) {
        return;
    }
    const std::string_view device_name = physical_device.getProperties(dispatch).deviceName.data();
    const NVSDK_NGX_FeatureDiscoveryInfo discovery = DiscoveryInfo();
    NVSDK_NGX_FeatureRequirement requirement{};
    if (const NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_GetFeatureRequirements(
            static_cast<VkInstance>(instance), static_cast<VkPhysicalDevice>(physical_device),
            &discovery, &requirement);
        NVSDK_NGX_FAILED(result)) {
        Disable(fmt::format("DLSS: NGX did not check {}: {}", device_name, ResultText(result)));
        return;
    }
    if (requirement.FeatureSupported != NVSDK_NGX_FeatureSupportResult_Supported) {
        Disable(fmt::format("DLSS is unavailable on {}: {}", device_name,
                            UnsupportedText(requirement.FeatureSupported)));
        return;
    }
    u32 count = 0;
    VkExtensionProperties* required{};
    if (const NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(
            static_cast<VkInstance>(instance), static_cast<VkPhysicalDevice>(physical_device),
            &discovery, &count, &required);
        NVSDK_NGX_FAILED(result)) {
        Disable(fmt::format("DLSS: NGX gave no device extensions: {}", ResultText(result)));
        return;
    }
    g_device_extension_names = NamesOf({required, count});
    const auto [extensions_result, available] =
        physical_device.enumerateDeviceExtensionProperties(nullptr, dispatch);
    if (const std::string lacking = Unavailable(g_device_extension_names, available);
        !lacking.empty()) {
        Disable(fmt::format("DLSS: {} lacks {}", device_name, lacking));
        return;
    }
    g_device_extensions.assign(create_info.ppEnabledExtensionNames,
                               create_info.ppEnabledExtensionNames +
                                   create_info.enabledExtensionCount);
    AppendMissing(g_device_extensions, g_device_extension_names);
    // NGX can ask for both buffer device address extensions, which a device must not enable
    // together.
    const auto named = [](std::string_view wanted) {
        return [wanted](const char* name) { return wanted == name; };
    };
    if (std::ranges::any_of(g_device_extensions,
                            named(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))) {
        std::erase_if(g_device_extensions, named(VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME));
    }
    create_info.enabledExtensionCount = static_cast<u32>(g_device_extensions.size());
    create_info.ppEnabledExtensionNames = g_device_extensions.data();
    LOG_INFO(Render_Vulkan, "GR2 DLSS: device extensions: {}",
             fmt::join(g_device_extension_names, ", "));
    g_stage.store(Stage::DeviceExtended, std::memory_order_release);
}

void OnDeviceCreated(vk::Instance instance, vk::PhysicalDevice physical_device, vk::Device device,
                     const vk::detail::DispatchLoaderDynamic& dispatch) {
    if (g_stage.load(std::memory_order_acquire) != Stage::DeviceExtended) {
        return;
    }
    LOG_INFO(Render_Vulkan, "GR2 DLSS: starting NGX");
    const Clock::time_point start = Clock::now();
    if (const NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_Init_with_ProjectID(
            ProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, EngineVersion, g_folder.c_str(),
            static_cast<VkInstance>(instance), static_cast<VkPhysicalDevice>(physical_device),
            static_cast<VkDevice>(device), dispatch.vkGetInstanceProcAddr,
            dispatch.vkGetDeviceProcAddr, &g_common, NVSDK_NGX_Version_API);
        NVSDK_NGX_FAILED(result)) {
        Disable(fmt::format("DLSS: NGX did not start: {}", ResultText(result)));
        return;
    }
    if (const NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_GetCapabilityParameters(&g_params);
        NVSDK_NGX_FAILED(result)) {
        Disable(fmt::format("DLSS: NGX gave no capabilities: {}", ResultText(result)));
        return;
    }
    int available = 0;
    NVSDK_NGX_Parameter_GetI(g_params, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
    if (available == 0) {
        int needs_driver = 0;
        u32 major = 0;
        u32 minor = 0;
        int init_result = static_cast<int>(NVSDK_NGX_Result_Fail);
        NVSDK_NGX_Parameter_GetI(g_params, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver,
                                 &needs_driver);
        NVSDK_NGX_Parameter_GetUI(g_params, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor,
                                  &major);
        NVSDK_NGX_Parameter_GetUI(g_params, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor,
                                  &minor);
        NVSDK_NGX_Parameter_GetI(g_params, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult,
                                 &init_result);
        Disable(needs_driver != 0
                    ? fmt::format("DLSS needs NVIDIA driver {}.{} or newer", major, minor)
                    : fmt::format("DLSS did not load: {}",
                                  ResultText(static_cast<NVSDK_NGX_Result>(init_result))));
        return;
    }
    g_device = device;
    g_stage.store(Stage::Running, std::memory_order_release);
    LOG_INFO(Render_Vulkan, "GR2 DLSS: NGX ready in {:.0f} ms",
             std::chrono::duration<float, std::milli>(Clock::now() - start).count());
}

bool DlssRunning() {
    return g_stage.load(std::memory_order_acquire) == Stage::Running;
}

std::string DlssBootNotice() {
    return g_notice.empty() ? "DLSS needs a restart: it was off when the game started" : g_notice;
}

std::optional<DlssRange> DlssOptimalRange(DlssMode mode, DlssPreset preset,
                                          vk::Extent2D output_size, std::string& notice) {
    SetPresets(preset);
    DlssRange range{};
    float sharpness = 0.0f;
    const NVSDK_NGX_Result result = NGX_DLSS_GET_OPTIMAL_SETTINGS(
        g_params, output_size.width, output_size.height,
        static_cast<NVSDK_NGX_PerfQuality_Value>(mode), &range.optimal.width, &range.optimal.height,
        &range.max.width, &range.max.height, &range.min.width, &range.min.height, &sharpness);
    if (NVSDK_NGX_FAILED(result)) {
        notice =
            fmt::format("DLSS {}: no optimal settings: {}", DlssModeName(mode), ResultText(result));
        return std::nullopt;
    }
    // DLSS gives no size for a mode it does not have.
    if (range.optimal.width == 0 || range.optimal.height == 0) {
        notice = fmt::format("DLSS {} for {}x{}: mode not available", DlssModeName(mode),
                             output_size.width, output_size.height);
        return std::nullopt;
    }
    return range;
}

void DlssSetOptions(DlssMode mode, DlssPreset preset) {
    g_mode = mode;
    g_preset = preset;
}

std::string DlssCreate(vk::CommandBuffer cmdbuf, vk::Extent2D render_size,
                       vk::Extent2D output_size) {
    SetPresets(g_preset);
    NVSDK_NGX_Parameter_SetUI(g_params, NVSDK_NGX_Parameter_FreeMemOnReleaseFeature, 1);
    NVSDK_NGX_DLSS_Create_Params create{};
    create.Feature.InWidth = render_size.width;
    create.Feature.InHeight = render_size.height;
    create.Feature.InTargetWidth = output_size.width;
    create.Feature.InTargetHeight = output_size.height;
    create.Feature.InPerfQualityValue = static_cast<NVSDK_NGX_PerfQuality_Value>(g_mode);
    create.InFeatureCreateFlags = FeatureFlags;
    if (const NVSDK_NGX_Result result = NGX_VULKAN_CREATE_DLSS_EXT1(
            static_cast<VkDevice>(g_device), static_cast<VkCommandBuffer>(cmdbuf), 1, 1, &g_feature,
            g_params, &create);
        NVSDK_NGX_FAILED(result)) {
        g_feature = nullptr;
        return fmt::format("DLSS: NGX did not make the feature: {}", ResultText(result));
    }
    LOG_INFO(Render_Vulkan, "GR2 DLSS: feature made: {} {}x{} -> {}x{}", DlssModeName(g_mode),
             render_size.width, render_size.height, output_size.width, output_size.height);
    return {};
}

std::string DlssEvaluate(vk::CommandBuffer cmdbuf, const DlssEvaluateInfo& info) {
    const VkCommandBuffer cmd = static_cast<VkCommandBuffer>(cmdbuf);
    if (g_feature == nullptr) {
        return "DLSS: no feature is made";
    }
    const Fsr4Images& images = info.images;
    NVSDK_NGX_Resource_VK color =
        ResourceOf(images.color, ImageFormats[0], info.render_size, false);
    NVSDK_NGX_Resource_VK motion =
        ResourceOf(images.motion_vectors, ImageFormats[1], info.render_size, false);
    NVSDK_NGX_Resource_VK depth =
        ResourceOf(images.depth, ImageFormats[2], info.render_size, false);
    NVSDK_NGX_Resource_VK output =
        ResourceOf(images.output, ImageFormats[3], info.output_size, true);
    NVSDK_NGX_VK_DLSS_Eval_Params evaluate{};
    evaluate.Feature.pInColor = &color;
    evaluate.Feature.pInOutput = &output;
    evaluate.pInDepth = &depth;
    evaluate.pInMotionVectors = &motion;
    evaluate.InJitterOffsetX = info.jitter_x;
    evaluate.InJitterOffsetY = info.jitter_y;
    evaluate.InRenderSubrectDimensions = {info.render_size.width, info.render_size.height};
    evaluate.InReset = info.reset ? 1 : 0;
    evaluate.InMVScaleX = info.motion_vector_scale_x;
    evaluate.InMVScaleY = info.motion_vector_scale_y;
    if (const NVSDK_NGX_Result result =
            NGX_VULKAN_EVALUATE_DLSS_EXT(cmd, g_feature, g_params, &evaluate);
        NVSDK_NGX_FAILED(result)) {
        return fmt::format("DLSS: NGX rejected the evaluation: {}", ResultText(result));
    }
    return {};
}

std::filesystem::path DlssDir() {
    return Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "dlss";
}

void DlssFree() {
    if (g_feature == nullptr) {
        return;
    }
    if (const NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_ReleaseFeature(g_feature);
        NVSDK_NGX_FAILED(result)) {
        LOG_WARNING(Render_Vulkan, "GR2 DLSS: NGX did not release the feature: {}",
                    ResultText(result));
    }
    g_feature = nullptr;
}

} // namespace Gr2Fsr3
