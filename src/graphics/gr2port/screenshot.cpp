// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "common/elf_info.h"
#include "common/io_file.h"
#include "common/logging/log.h"
#include "common/path_util.h"
#include "common/stb.h"
#include "graphics/gr2port/menu_kit.h"
#include "graphics/gr2port/screenshot.h"
#include "graphics/gr2port/settings.h"
#include "graphics/gr2port/ui.h"
#include "imgui/notifications_layer.h"

namespace Gr2Port::Screenshot {

namespace {

constexpr u32 NoSlot = ~0u;
constexpr u32 PixelBytes = 4;
constexpr float NoticeSeconds = 3.0f;
// The game's sound sys_camera_shutter, which its photo mode plays.
constexpr u32 ShutterSound = 0x040ff9f6;
// The table the game looks sounds up in, null until the game's main loop makes it.
constexpr VAddr SoundTable = 0x1f207a0;
// void FUN_014cd100(record, sound): the sound's record, with a negative bank for no such sound.
constexpr VAddr FindSoundFunction = 0x14cd100;
// int FUN_014e3370(volume, bank, index, categories, held): starts the sound unless held is set and
// returns its instance, or a negative number.
constexpr VAddr StartSoundFunction = 0x14e3370;
// int sceSndxInstanceSetCategoryFlags(instance, categories).
constexpr VAddr SetCategoriesFunction = 0x675cd0;
// The category of the game's menu sounds. The shutter's own category is silent on the title screen
// and held in the pause menu, and the menu sounds' category plays in both.
constexpr u32 MenuCategory = 0x8000;

struct SoundRecord {
    s32 bank;
    u32 unused;
    u64 data;
    u32 index;
    u32 categories;
    u64 tail;
};
static_assert(sizeof(SoundRecord) == 0x20);

using FindSound = void PS4_SYSV_ABI (*)(SoundRecord* record, u32 sound);
using StartSound = s32 PS4_SYSV_ABI (*)(float volume, s32 bank, u32 index, u32 categories, u8 held);
using SetCategories = s32 PS4_SYSV_ABI (*)(s32 instance, u32 categories);

std::atomic<bool> g_requested{false};
std::atomic<bool> g_shutter_ready{false};
std::atomic<bool> g_shutter{false};
// The render thread's copy in flight, and the screenshots taken in this run.
Buffer g_buffer;
u32 g_slot = NoSlot;
vk::Extent2D g_extent;
u64 g_taken = 0;

// The game's serial, the local time to the millisecond and the screenshot's number in this run.
std::filesystem::path NextPath() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() %
        1000;
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    const std::string name = fmt::format("{}_{:04}{:02}{:02}_{:02}{:02}{:02}_{:03}_game_{:06}.png",
                                         Common::ElfInfo::Instance().GameSerial(),
                                         local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                                         local.tm_hour, local.tm_min, local.tm_sec, ms, g_taken++);
    return Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "screenshots" / name;
}

// Writes the pixels as a PNG at path off the render thread, and shows it in a notice.
void Save(std::vector<u8> pixels, vk::Extent2D extent, std::filesystem::path path) {
    std::thread([pixels = std::move(pixels), extent, path = std::move(path)]() mutable {
        for (size_t i = 0; i < pixels.size(); i += PixelBytes) {
            pixels[i + 3] = 0xff;
        }
        std::vector<u8> png;
        stbi_write_png_to_func(
            [](void* context, void* data, int size) {
                auto* out = static_cast<std::vector<u8>*>(context);
                const auto* bytes = static_cast<const u8*>(data);
                out->insert(out->end(), bytes, bytes + size);
            },
            &png, static_cast<int>(extent.width), static_cast<int>(extent.height), PixelBytes,
            pixels.data(), static_cast<int>(extent.width * PixelBytes));
        const std::string shown = Common::FS::PathToUTF8String(path);
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        Common::FS::IOFile file{path, Common::FS::FileAccessMode::Create};
        if (png.empty() || !file.IsOpen() ||
            file.WriteRaw<u8>(png.data(), png.size()) != png.size()) {
            LOG_ERROR(Render_Vulkan, "GR2port: the screenshot was not saved to {}", shown);
            return;
        }
        LOG_INFO(Render_Vulkan, "GR2port: saved a screenshot of {}x{} to {}", extent.width,
                 extent.height, shown);
        if (Settings::Get().game.screenshot_popup) {
            shadNotifications::QueueNotification("Saved screenshot:\n" + shown, NoticeSeconds,
                                                 shadNotifications::position::BottomRight,
                                                 std::move(png));
        }
    }).detach();
}

} // namespace

void Install() {
    g_shutter_ready.store(true, std::memory_order_relaxed);
}

void Request() {
    g_requested.store(true, std::memory_order_relaxed);
    g_shutter.store(g_shutter_ready.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

void PlayShutter() {
    if (!g_shutter.load(std::memory_order_relaxed) ||
        !g_shutter.exchange(false, std::memory_order_relaxed) ||
        MenuKit::Read<u64>(MenuKit::Game(SoundTable)) == 0) {
        return;
    }
    SoundRecord sound{};
    MenuKit::Call<FindSound>(FindSoundFunction)(&sound, ShutterSound);
    if (sound.bank < 0) {
        return;
    }
    const s32 instance = MenuKit::Call<StartSound>(StartSoundFunction)(
        1.0f, sound.bank, sound.index, sound.categories, 0);
    if (instance >= 0) {
        MenuKit::Call<SetCategories>(SetCategoriesFunction)(instance, MenuCategory);
    }
}

void Record(Memory& memory, vk::CommandBuffer cmd, const Scene::Header& header, u32 slot) {
    if (g_slot != NoSlot) {
        return;
    }
    const vk::Extent2D extent = Ui::TargetExtent(header.ui.picture);
    const u64 bytes = u64{extent.width} * extent.height * PixelBytes;
    if (bytes == 0 || !g_requested.exchange(false, std::memory_order_relaxed)) {
        return;
    }
    if (g_buffer.size < bytes) {
        memory.Destroy(g_buffer);
        g_buffer =
            memory.CreateBuffer(bytes, vk::BufferUsageFlagBits::eTransferDst, Placement::Readback);
        if (g_buffer.mapped == nullptr) {
            LOG_ERROR(Render_Vulkan, "GR2port: no memory to read back a screenshot of {}x{}",
                      extent.width, extent.height);
            return;
        }
    }
    Ui::CopyTarget(cmd, header.ui.picture, g_buffer.buffer);
    g_slot = slot;
    g_extent = extent;
}

void Collect(u32 slot) {
    if (g_slot != slot) {
        return;
    }
    g_slot = NoSlot;
    Save(std::vector<u8>(g_buffer.mapped,
                         g_buffer.mapped + u64{g_extent.width} * g_extent.height * PixelBytes),
         g_extent, NextPath());
}

} // namespace Gr2Port::Screenshot
