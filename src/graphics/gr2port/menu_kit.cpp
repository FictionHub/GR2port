// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "core/language/language.h"

#include "graphics/gr2port/menu_kit.h"

namespace Gr2Port::MenuKit {

namespace {

// The code of the language the game shows, such as "EN" or "BP".
constexpr VAddr LanguageCode = 0x1cdf800;
// void FUN_014e0ea0(float volume, u32 sound, 0)
constexpr VAddr PlaySound = 0x14e0ea0;
// void FUN_011e09b0(key, int kind, int choice)
constexpr VAddr OpenDialogFunction = 0x11e09b0;
// void FUN_013e0e20(float speed, float start, clip, u32 node, u32 animation)
constexpr VAddr Play = 0x13e0e20;
// The open dialog and its answer.
constexpr VAddr Dialog = 0x1eed678;
constexpr u64 DialogAnswer = 0x10;

// A clip's resource: the count of its nodes and their descriptors, each with its name's hash; and
// the count of node states.
constexpr u64 ResourceNodeCount = 0xc8;
constexpr u64 ResourceNodes = 0xf0;
constexpr u64 NodeHash = 0x14;
constexpr u64 ResourceStateCount = 0xe0;
// A node's animation state: its clip's animations, the animation that plays and its speed.
constexpr u64 NodeStateBytes = 0x48;
constexpr u64 StateAnimations = 0x08;
constexpr u64 AnimationCount = 0x1c;
constexpr u64 StateSpeed = 0x10;
constexpr u64 StateAnimation = 0x2c;

using SoundFunction = void PS4_SYSV_ABI (*)(float volume, u32 sound, u64 unused);
using DialogFunction = void PS4_SYSV_ABI (*)(const char* key, s32 kind, s32 choice);
using PlayFunction = void PS4_SYSV_ABI (*)(float speed, float start, u64 clip, u32 node,
                                           u32 animation);

// A text record of the game's bank: a 0x1c byte header (the count of characters, the count of
// bytes, the count of attributes, then the style), the UTF-8 text and its terminator padded to 4
// bytes, then the attributes, 8 bytes each: a first character, an end character and four bytes
// of style.
constexpr size_t RecordHeader = 0x1c;
constexpr size_t RecordAttribute = 8;

// The console language of each language the game has text in, but its two Englishes.
struct GameLanguage {
    const char* code;
    int console_language;
};
constexpr std::array GameLanguages{
    GameLanguage{"JP", 0},  GameLanguage{"FR", 2},  GameLanguage{"ES", 3},  GameLanguage{"DE", 4},
    GameLanguage{"IT", 5},  GameLanguage{"NL", 6},  GameLanguage{"PT", 7},  GameLanguage{"RU", 8},
    GameLanguage{"KR", 9},  GameLanguage{"CT", 10}, GameLanguage{"CS", 11}, GameLanguage{"PL", 16},
    GameLanguage{"BP", 17}, GameLanguage{"MX", 20}, GameLanguage{"CA", 22},
};

VAddr g_base = 0;

} // namespace

void SetBase(VAddr module_base) {
    g_base = module_base;
}

VAddr Game(VAddr address) {
    return g_base + address - GoldHenBase;
}

std::vector<u8> MakeRecord(u64 model, std::string_view text) {
    const u16 characters = static_cast<u16>(
        std::ranges::count_if(text, [](char byte) { return (byte & 0xc0) != 0x80; }));
    const u16 bytes = static_cast<u16>(text.size());
    const s16 attributes = Read<s16>(model + 4) > 0 ? 1 : 0;
    const size_t attribute_offset = RecordHeader + ((text.size() + 4) & ~size_t{3});
    std::vector<u8> record(attribute_offset + attributes * RecordAttribute);
    std::memcpy(record.data(), reinterpret_cast<const void*>(model), RecordHeader);
    std::memcpy(record.data(), &characters, sizeof(characters));
    std::memcpy(record.data() + 2, &bytes, sizeof(bytes));
    std::memcpy(record.data() + 4, &attributes, sizeof(attributes));
    std::memcpy(record.data() + RecordHeader, text.data(), text.size());
    if (attributes != 0) {
        const u64 model_attribute = model + RecordHeader + ((Read<u16>(model + 2) + 4) & ~3);
        u8* const attribute = record.data() + attribute_offset;
        std::memcpy(attribute, reinterpret_cast<const void*>(model_attribute), RecordAttribute);
        const u16 first = 0;
        std::memcpy(attribute, &first, sizeof(first));
        std::memcpy(attribute + 2, &characters, sizeof(characters));
    }
    return record;
}

std::string Shown(std::string_view english) {
    const char* const code = Read<const char*>(Game(LanguageCode));
    if (code != nullptr) {
        for (const GameLanguage& language : GameLanguages) {
            if (std::strcmp(code, language.code) == 0) {
                return Language::TextIn(language.console_language, english);
            }
        }
    }
    return std::string(english);
}

u32 ButtonBit(VAddr number) {
    const u32 bit = Read<u32>(Game(number));
    return bit < 0x1b ? 1u << bit : 0;
}

void Sound(u32 sound) {
    Call<SoundFunction>(PlaySound)(1.0f, sound, 0);
}

void OpenDialog(const char* key, s32 kind, s32 choice) {
    Call<DialogFunction>(OpenDialogFunction)(key, kind, choice);
}

s32 Answer() {
    return Read<s32>(Read<u64>(Game(Dialog)) + DialogAnswer);
}

void PlayTrack(u64 handle, size_t track_index, size_t animation) {
    Track* const first = Read<Track*>(handle + HandleFirstTrack);
    const Track* const last = Read<const Track*>(handle + HandleLastTrack);
    if (static_cast<size_t>(last - first) <= track_index) {
        return;
    }
    Track& track = first[track_index];
    if (static_cast<size_t>(track.last - track.first) <= animation) {
        return;
    }
    Call<PlayFunction>(Play)(1.0f / 30.0f, 0.0f, Read<u64>(handle + HandleClip), track.node,
                             track.first[animation]);
    track.reversed = 0;
    track.current = static_cast<u32>(animation);
}

bool Playing(u64 handle, size_t track_index, size_t animation) {
    const Track* const first = Read<const Track*>(handle + HandleFirstTrack);
    const Track* const last = Read<const Track*>(handle + HandleLastTrack);
    const u64 clip = Read<u64>(handle + HandleClip);
    if (static_cast<size_t>(last - first) <= track_index || clip == 0) {
        return false;
    }
    const Track& track = first[track_index];
    if (static_cast<size_t>(track.last - track.first) <= animation ||
        track.node >= Read<u32>(Read<u64>(clip + ClipResource) + ResourceStateCount)) {
        return false;
    }
    const u64 state = Read<u64>(clip + ClipNodeStates) + u64{track.node} * NodeStateBytes;
    const u32 id = track.first[animation];
    if (id >= Read<u32>(Read<u64>(state + StateAnimations) + AnimationCount) ||
        Read<u32>(state + StateAnimation) != id) {
        return false;
    }
    const float speed = Read<float>(state + StateSpeed);
    return track.reversed == 0 ? speed > 0.0f : speed < 0.0f;
}

u32 FindNode(u64 clip, u32 hash) {
    if (clip == 0) {
        return NoNode;
    }
    const u64 resource = Read<u64>(clip + ClipResource);
    const u32 count = Read<u32>(resource + ResourceNodeCount);
    const u64 nodes = Read<u64>(resource + ResourceNodes);
    for (u32 i = 0; i < count; ++i) {
        if (Read<u32>(Read<u64>(nodes + u64{i} * 8) + NodeHash) == hash) {
            return i;
        }
    }
    return NoNode;
}

} // namespace Gr2Port::MenuKit
