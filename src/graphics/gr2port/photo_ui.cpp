// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "common/logging/log.h"
#include "graphics/gr2port/hooks.h"
#include "graphics/gr2port/menu_kit.h"
#include "graphics/gr2port/photo_ui.h"

namespace Gr2Port::PhotoUi {

namespace {

// A byte that is set while the camera is out, from the press that takes it out to the press that
// puts it away (HUD.isPhotoMode, FUN_011b0f90, reads it).
constexpr VAddr CameraOut = 0x1ee3105;

// R3 in the pad's button bits.
constexpr u32 Button = 0x4;

// void FUN_011ba250(list): the control list's frame, which hands each of its slots the row the
// slot asks for.
constexpr Hooks::Entry ListFrame{0x11ba250, 6, Hooks::FramePrologue};
// The rows the list's slots ask for, a pointer or null for each. Slots 6 to 13 run down the left
// column; slot 11 holds the Filter Effect row and slot 12, the one below it, is empty.
constexpr u64 SlotRows = 0x80;
constexpr u32 FilterSlot = 11;
constexpr u32 RowSlot = 12;

// A row of the control list, as the game's own rows are.
struct ListRow {
    // The game's numbers of the buttons whose presses play the row's press, or -1.
    s32 button;
    s32 second_button;
    // The row's text in the game's table of them, or -1 for the text of key.
    s32 text;
    // 0 when the row draws the Filter Effect header above itself.
    s32 header;
    const char* key;
    // The icon's textures: the small one and the one the row shows.
    const char* small_icon;
    const char* icon;
};
static_assert(sizeof(ListRow) == 0x28);

// The game's number of R3, and its R3 icon, which no small icon of its own comes with.
constexpr s32 R3Number = 9;
constexpr char R3Icon[] = "photomode_key_20";
constexpr char RowKey[] = "gr2port_photo_hide";
// The game's text whose record gives the row's text its style: Put Away Camera's, in the same
// column.
constexpr char ModelKey[] = "ui_camera_guide_06";
constexpr char English[] = "Hide UI";

constexpr ListRow HideRow{R3Number, -1, -1, -1, RowKey, R3Icon, R3Icon};

std::atomic<VAddr> g_base{0};
std::atomic<bool> g_hidden{false};
// The last buttons read from each pad's handle.
std::mutex g_mutex;
std::unordered_map<s32, u32> g_last;
std::mutex g_record_mutex;
std::vector<u8> g_record;

// Before the list's frame: asks for the Hide UI row below the Filter Effect row while there is
// one, and takes the row out once there is none.
void PS4_SYSV_ABI OnListFrame(u32, const Hooks::Registers* registers, u64) {
    const VAddr slots = registers->rdi + SlotRows;
    const auto* filter = MenuKit::Read<const ListRow*>(slots + FilterSlot * sizeof(VAddr));
    const auto* row = MenuKit::Read<const ListRow*>(slots + RowSlot * sizeof(VAddr));
    const bool wanted = filter != nullptr && filter->header == 0;
    if (wanted && row == nullptr) {
        MenuKit::Write(slots + RowSlot * sizeof(VAddr), &HideRow);
    } else if (!wanted && row == &HideRow) {
        MenuKit::Write<const ListRow*>(slots + RowSlot * sizeof(VAddr), nullptr);
    }
}

} // namespace

void Install(VAddr module_base) {
    g_base.store(module_base, std::memory_order_relaxed);
}

void OnButtons(s32 handle, u32 buttons) {
    const VAddr base = g_base.load(std::memory_order_relaxed);
    if (base == 0) {
        return;
    }
    std::scoped_lock lock{g_mutex};
    u32& last = g_last[handle];
    const bool pressed = (buttons & ~last & Button) != 0;
    last = buttons;
    if (*Hooks::Code(base, CameraOut) == 0) {
        g_hidden.store(false, std::memory_order_relaxed);
    } else if (pressed) {
        g_hidden.store(!g_hidden.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
}

bool AddRow(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity) {
    if (!Hooks::Hooked(Hooks::ObserveEntry(module_base, ListFrame, c, capacity, &OnListFrame, 0))) {
        LOG_ERROR(Render_Vulkan, "GR2port: the photo mode's control list is not the expected code");
        return false;
    }
    return true;
}

u64 FindText(u64 texts, const char* key, GraphicsMenu::FindTextFunction find) {
    if (std::strcmp(key, RowKey) != 0) {
        return find(texts, key);
    }
    std::scoped_lock lock{g_record_mutex};
    if (g_record.empty()) {
        const u64 model = find(texts, ModelKey);
        if (model == 0) {
            return 0;
        }
        g_record = MenuKit::MakeRecord(model, MenuKit::Shown(English));
    }
    return reinterpret_cast<u64>(g_record.data());
}

bool Hidden() {
    return g_hidden.load(std::memory_order_relaxed);
}

} // namespace Gr2Port::PhotoUi
