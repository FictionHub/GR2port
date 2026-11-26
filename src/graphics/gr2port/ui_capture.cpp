// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iterator>
#include <mutex>
#include <numbers>
#include <span>
#include <vector>

#include <fmt/ranges.h>

#include "common/logging/log.h"
#include "graphics/gr2port/capture_math.h"
#include "graphics/gr2port/hooks.h"
#include "graphics/gr2port/marker_fit.h"
#include "graphics/gr2port/photo_ui.h"
#include "graphics/gr2port/textures.h"
#include "graphics/gr2port/ui.h"
#include "graphics/gr2port/ui_capture.h"
#include "graphics/gr2port/ui_items.h"

namespace Gr2Port::UiCapture {

namespace {

using Capture::Load;

// The index of the draw thread's context of the last frame, which is its back buffer's.
constexpr VAddr PreviousContextIndex = 0x1f585f4;
// The frame's back buffer, of three. A target object holds its size at +0x14 and its T# at +0x30.
constexpr VAddr BackBuffers = 0x1f31260;
constexpr u64 BackBufferBytes = 0x118;
constexpr u64 TargetWidth = 0x14;
constexpr u64 TargetHeight = 0x16;
constexpr u64 TargetSharp = 0x30;
// The photo manager, whose target object at +0x580 FUN_011afe90 draws each shot into.
constexpr VAddr PhotoManager = 0x1eed278;
constexpr u64 PhotoObject = 0x580;
// The sampler the filter's view reads the picture with, the shot's: wrapped and filtered linearly.
constexpr u32 PreviewSampler = 0;
// The film frame the filter lays over the photo: its texture object, with the T# and S# the draw
// binds while the object's two checks pass.
constexpr VAddr FilmFrame = 0x1ee31c8;
constexpr u64 FilmMemory = 0xb8;
constexpr u64 FilmState = 0xec;
constexpr u64 FilmSharp = 0xe0;
constexpr u64 FilmSampler = 0x100;
// The filters, one object each from the first: each holds its curves, the filter program's
// constants, at +0x10.
constexpr VAddr FilterObjects = 0x1ee31d0;
constexpr u64 FilterBytes = 0x2020;
constexpr u64 FilterCurves = 0x10;
constexpr u64 CurveBytes = 0x2000;
// The shot's rectangle, a strip of four vertices: the position, the photo's coordinates in the
// frame, the coordinates in the film frame, then the colour, which the filter does not read.
constexpr VAddr PhotoMesh = 0x18adc40;
constexpr u32 PhotoVertices = 4;
constexpr u32 PhotoFloats = 11;
constexpr float PhotoSide = 1024.0f;
// The vertex program's constants as the shot sets them: its master colour.
constexpr std::array<float, 4> MasterColor{1.0f, 1.0f, 1.0f, 1.0f};

// The photo viewfinder's pieces, known by their textures' size and data format: the vignette over
// the picture, the dimmed surround and the window's soft edge, the window's halves, the guide
// frame's sides and corners, the cross hair and the shot counter's digits.
struct FinderTexture {
    u32 width;
    u32 height;
    u32 format;
    friend bool operator==(const FinderTexture&, const FinderTexture&) = default;
};
constexpr FinderTexture FinderVignette{960, 540, 41};
constexpr FinderTexture FinderGuide{808, 20, 10};
constexpr std::array FinderTextures{
    FinderVignette,
    FinderTexture{512, 512, 41},
    FinderTexture{256, 512, 41},
    FinderGuide,
    FinderTexture{32, 856, 41},
    FinderTexture{88, 88, 10},
    FinderTexture{192, 192, 41},
    FinderTexture{1280, 256, 41},
};

// The game's writers of the screen drawing's state and draws, which write only their packets.
// UiItems reads the screen drawing from its draw items, so they return at once: FUN_00413c90 and
// FUN_00413d50 set and update the pixel shader, FUN_00413db0 and FUN_00413e90 the vertex shader,
// FUN_00413f00 the export shader, FUN_00414ec0 and FUN_00414fe0 a stage's user data, FUN_00411610 a
// target's blend, FUN_00411d60 a colour target, FUN_00415120 a viewport, FUN_004152c0 the screen
// scissor, FUN_00412520 the primitive type, FUN_00413b90 the index buffer, FUN_00413b10 the index
// size, and FUN_00412820 and FUN_00412c50 draw.
constexpr std::array Writers{
    Hooks::Entry{0x413c90, 6, Hooks::FramePrologueR14},
    Hooks::Entry{0x413d50, 6, Hooks::FramePrologueR14},
    Hooks::Entry{0x413db0, 6, Hooks::FramePrologue},
    Hooks::Entry{0x413e90, 6, Hooks::FramePrologue},
    Hooks::Entry{0x413f00, 6, Hooks::FramePrologue},
    Hooks::Entry{0x414ec0, 6, Hooks::FramePrologue},
    Hooks::Entry{0x414fe0, 6, Hooks::FramePrologue},
    Hooks::Entry{0x411610, 6, Hooks::FramePrologue},
    Hooks::Entry{0x411d60, 6, Hooks::FramePrologue},
    Hooks::Entry{0x415120, 6, Hooks::FramePrologue},
    Hooks::Entry{0x4152c0, 6, Hooks::FramePrologue},
    Hooks::Entry{0x412520, 6, Hooks::FramePrologueR14},
    Hooks::Entry{0x413b90, 6, Hooks::FramePrologueR14},
    Hooks::Entry{0x413b10, 6, Hooks::FramePrologue},
    Hooks::Entry{0x412820, 6, Hooks::FramePrologue},
    Hooks::Entry{0x412c50, 6, Hooks::FramePrologue},
};

struct VertexProgram {
    // The inputs the program reads, one bit per input slot.
    u32 inputs;
    bool constants;
    bool palette;
};
// In Scene::UiVertexProgram's order. The stroke's constants are its export program's, and the
// geometry program's constants take the palette's place.
constexpr std::array<VertexProgram, static_cast<u32>(Scene::UiVertexProgram::Count)> VertexPrograms{
    {
        {0x7, false, false},
        {0x0, false, false},
        {0x7, true, false},
        {0xf, true, false},
        {0x7, true, false},
        {0x1f, true, true},
        {0x3f, true, true},
        {0xf, true, false},
        {0x5, true, true},
    }};

struct PixelProgram {
    u32 textures;
    bool constants;
};
// In Scene::UiPixelProgram's order.
constexpr std::array<PixelProgram, static_cast<u32>(Scene::UiPixelProgram::Count)> PixelPrograms{{
    {0, false},
    {1, false},
    {0, false},
    {1, true},
    {1, true},
    {1, true},
    {1, true},
    {2, true},
    {1, true},
    {2, true},
    {1, true},
    {2, true},
    {0, true},
    {2, true},
    {0, true},
}};

// The engine's blend of the menus' own draws: the colour comes multiplied by its alpha.
constexpr u32 PremultipliedBlend = 0x65000501;
// The canvas the game lays its menus out on, which fills the picture.
constexpr float CanvasWidth = 1920.0f;
constexpr float CanvasHeight = 1080.0f;
// A vertex of a shape this file draws by itself: a position, a texture coordinate and a colour,
// in the formats the pass program's three inputs take as three, two and four floats.
constexpr u32 ShapeFloats = 9;
constexpr u32 ShapePosition = 13u << 15 | 7u << 12 | 4u | 5u << 3 | 6u << 6 | 1u << 9;
constexpr u32 ShapeCoordinate = 11u << 15 | 7u << 12 | 4u | 5u << 3 | 1u << 9;
constexpr u32 ShapeColor = 14u << 15 | 7u << 12 | 4u | 5u << 3 | 6u << 6 | 7u << 9;
// The steps of a rounded end, the colours of the scroll bar's track, of the lighter line along
// the track's middle and of the thumb, the line's share of the width, and the time a scroll bar
// stays without being set again.
constexpr u32 EndSteps = 6;
constexpr std::array<float, 4> TrackColor{0.0f, 0.0f, 0.0f, 0.55f};
constexpr std::array<float, 4> LineColor{0.8f, 0.8f, 0.86f, 0.3f};
constexpr std::array<float, 4> ThumbColor{0.93f, 0.79f, 0.47f, 1.0f};
constexpr float LineShare = 0.3f;
constexpr std::chrono::milliseconds BarLife{250};

constexpr u32 ReportFrames = 600;
constexpr u32 LoggedUnknowns = 64;
// The buffers a frame's draws copied, by the game's address: an open table that each frame's
// writer empties by moving to a new generation. Draws often share their vertices and indices.
constexpr u32 SharedSlots = 8192;
constexpr u32 SharedProbes = 16;
struct SharedCopy {
    VAddr from;
    u64 bytes;
    u64 to;
    u32 generation;
};
std::array<SharedCopy, SharedSlots> g_shared{};
u32 g_shared_generation = 0;
// TextureHandle's answer for a texture the renderer does not have yet.
constexpr u32 NotReady = ~0u;

// The draw thread alone uses what follows: whether the frame graph ran in the frame.
VAddr g_base = 0;
bool g_frame_graph = false;

// Kept through the run, as the renderer keeps an image per target.
std::array<Scene::UiTarget, Scene::MaxUiTargets> g_targets{};
u32 g_target_count = 0;

std::array<Scene::UiRun, Scene::MaxUiRuns> g_runs;
// Per target, the frame that last wrote it, plus one. A write after FreshGap frames without one
// is fresh: the game keeps what it drew there for later frames.
constexpr u64 FreshGap = 16;
std::array<u64, Scene::MaxUiTargets> g_written{};
// The targets the frame wrote so far, and the back buffers seen so far.
u32 g_frame_writes = 0;
u32 g_back_buffers = 0;
// The targets the game's movie program writes, each of which holds a movie's latest frame.
u32 g_movie_targets = 0;
std::array<Scene::VertexCommand, Scene::MaxUiDraws> g_commands;
std::array<u32, Scene::MaxUiDynamicTextures> g_dynamic;

u32 g_logged_programs = 0;
u32 g_logged_primitives = 0;
u32 g_logged_drops = 0;
u32 g_logged_textures = 0;
std::array<u64, LoggedUnknowns> g_unknown_textures{};
u32 g_frames = 0;
u32 g_world_frames = 0;
u64 g_draws = 0;
u64 g_runs_total = 0;
u64 g_bytes = 0;
u64 g_unknown = 0;
u64 g_unported = 0;
u64 g_dropped = 0;
u64 g_write_ns = 0;
// Whether this frame, and the last, drew the viewfinder's guide frame: the camera's view, whose
// draws of the other FinderTextures are the viewfinder's. Gameplay and the pause menu draw the
// vignette too, and the pause menu draws textures of other pieces' shapes.
bool g_finder_frame = false;
bool g_finder_last = false;

std::mutex g_bar_mutex;
ScrollBar g_bar;
std::chrono::steady_clock::time_point g_bar_set;

struct Buffer {
    u64 base;
    u32 stride;
    u32 elements;
    u32 word3;
};

// The bytes a buffer's elements span: the last element ends at its format's size.
u64 BufferBytes(const Buffer& buffer) {
    u64 element = 4;
    switch (buffer.word3 >> 15 & 15) {
    case 14:
        element = 16;
        break;
    case 13:
        element = 12;
        break;
    case 11:
    case 12:
        element = 8;
        break;
    default:
        break;
    }
    return u64{buffer.stride} * (std::max(buffer.elements, 1u) - 1) + element;
}

// The bytes of a vertex input's first vertices elements.
u64 InputBytes(Buffer buffer, u32 vertices) {
    buffer.elements = std::min(buffer.elements, std::max(vertices, 1u));
    return BufferBytes(buffer);
}

u64 SharpAddress(const u32* sharp) {
    return (u64{sharp[1] & 0xff} << 32 | sharp[0]) << 8;
}

// The GoldHEN address of an address in the executable, for the logs.
u64 GameAddress(VAddr address) {
    return address >= g_base && address < g_base + 0x2000000 ? address - g_base + Hooks::CodeBase
                                                             : address;
}

u32 FindTarget(u64 base, u32 width, u32 height) {
    for (u32 i = 0; i < g_target_count; i++) {
        if (g_targets[i].base == base) {
            return i;
        }
    }
    if (g_target_count == g_targets.size() || base == 0 || width == 0 || height == 0) {
        return Scene::MaxUiTargets;
    }
    g_targets[g_target_count] = {.base = base, .width = width, .height = height};
    LOG_INFO(Render_Vulkan, "GR2port UI: target {} at {:#x}, {}x{}", g_target_count, base, width,
             height);
    return g_target_count++;
}

// A target object's target.
u32 ObjectTarget(VAddr object) {
    const auto sharp = Load<std::array<u32, 8>>(object + TargetSharp);
    return FindTarget(SharpAddress(sharp.data()), Load<u16>(object + TargetWidth),
                      Load<u16>(object + TargetHeight));
}

// An S#'s sampler as Ui's samplers number them: bit 0 clamps, bit 1 filters by point.
u32 SamplerOf(const u32* words) {
    const u32 clamp = (words[0] & 7) != 0 ? 1 : 0;
    const u32 point = (words[2] >> 20 & 3) == 0 ? 2 : 0;
    return clamp | point;
}

// The target of the photo the frame drew, plus one, or 0.
u32 PhotoTarget(const Scene::UiHeader& ui) {
    const VAddr manager = Load<VAddr>(g_base + PhotoManager - Hooks::CodeBase);
    if (manager == 0) {
        return 0;
    }
    const auto sharp = Load<std::array<u32, 8>>(manager + PhotoObject + TargetSharp);
    const u64 base = SharpAddress(sharp.data());
    for (u32 i = 0; i < ui.runs; i++) {
        const u32 target = g_runs[i].target;
        if (target < g_target_count && g_targets[target].base == base) {
            return target + 1;
        }
    }
    return 0;
}

class Writer {
public:
    Writer(u8* packet_, u64 address_, Scene::UiHeader& ui_)
        : packet{packet_}, address{address_}, ui{ui_} {
        g_shared_generation++;
    }

    void Draw(const UiItems::Draw& item);
    void Filter(u32 target);
    void Shape(std::span<const float> vertices, u32 target);
    void Finish();

    u64 Used() const {
        return used;
    }

private:
    u64 Copy(VAddr from, u64 bytes);
    u64 CopyShared(VAddr from, u64 bytes);
    u64 CopyIndices(VAddr from, u32 first, u32 count, u32 bits, u32& lowest, u32& highest);
    bool CopyInputs(std::array<Buffer, Scene::UiAttributes> buffers, u32 inputs, u32 first_vertex,
                    u32 vertices, Scene::UiDraw& draw);
    u32 TextureHandle(const u32* sharp, bool* linear = nullptr);
    void AddRun(const Scene::UiRun& state);

    u8* packet;
    u64 address;
    Scene::UiHeader& ui;
    u64 used = 0;
    bool filtered = false;
};

u64 Writer::Copy(VAddr from, u64 bytes) {
    constexpr u64 Alignment = 16;
    if (from == 0 || bytes == 0 || used + bytes > Scene::MaxUiDataBytes) {
        return 0;
    }
    const u64 at = used;
    std::memcpy(packet + Scene::UiDataOffset + at, reinterpret_cast<const void*>(from), bytes);
    used = (used + bytes + Alignment - 1) & ~(Alignment - 1);
    return address + Scene::UiDataOffset + at;
}

u64 Writer::CopyShared(VAddr from, u64 bytes) {
    u32 slot = static_cast<u32>((from >> 4) * 0x9e3779b97f4a7c15ull >> 51);
    for (u32 probe = 0; probe < SharedProbes; probe++, slot = (slot + 1) % SharedSlots) {
        SharedCopy& entry = g_shared[slot];
        if (entry.generation != g_shared_generation) {
            const u64 to = Copy(from, bytes);
            if (to != 0) {
                entry = {from, bytes, to, g_shared_generation};
            }
            return to;
        }
        if (entry.from == from && entry.bytes >= bytes) {
            return entry.to;
        }
    }
    return Copy(from, bytes);
}

// The game's UI draws share one large index buffer, each from its own first index, and one
// large vertex buffer. A draw's indices are copied from its first one, less the lowest, so only
// the vertices from the lowest to the highest are copied.
u64 Writer::CopyIndices(VAddr from, u32 first, u32 count, u32 bits, u32& lowest, u32& highest) {
    constexpr u64 Alignment = 16;
    const u64 size = bits / 8;
    const u64 bytes = u64{count} * size;
    if (from == 0 || count == 0 || used + bytes > Scene::MaxUiDataBytes) {
        return 0;
    }
    lowest = ~0u;
    highest = 0;
    for (u32 i = 0; i < count; i++) {
        const VAddr at = from + (u64{first} + i) * size;
        const u32 index = bits == 32 ? Load<u32>(at) : Load<u16>(at);
        lowest = std::min(lowest, index);
        highest = std::max(highest, index);
    }
    u8* const out = packet + Scene::UiDataOffset + used;
    for (u32 i = 0; i < count; i++) {
        const VAddr at = from + (u64{first} + i) * size;
        if (bits == 32) {
            const u32 index = Load<u32>(at) - lowest;
            std::memcpy(out + u64{i} * 4, &index, sizeof(index));
        } else {
            const u16 index = static_cast<u16>(Load<u16>(at) - lowest);
            std::memcpy(out + u64{i} * 2, &index, sizeof(index));
        }
    }
    const u64 at = used;
    used = (used + bytes + Alignment - 1) & ~(Alignment - 1);
    return address + Scene::UiDataOffset + at;
}

// The inputs of one vertex usually lie in one buffer, so their range is copied once. Only the
// vertices the draw reads, from first_vertex on, are copied.
bool Writer::CopyInputs(std::array<Buffer, Scene::UiAttributes> buffers, u32 inputs,
                        u32 first_vertex, u32 vertices, Scene::UiDraw& draw) {
    u64 low = ~u64{0};
    u64 high = 0;
    u64 total = 0;
    for (u32 i = 0; i < Scene::UiAttributes; i++) {
        if ((inputs >> i & 1) == 0) {
            continue;
        }
        if (first_vertex >= std::max(buffers[i].elements, 1u)) {
            return false;
        }
        buffers[i].base += u64{first_vertex} * buffers[i].stride;
        buffers[i].elements -= std::min(buffers[i].elements, first_vertex);
        const u64 bytes = InputBytes(buffers[i], vertices);
        low = std::min(low, buffers[i].base);
        high = std::max(high, buffers[i].base + bytes);
        total += bytes;
    }
    const bool together = high - low <= total;
    const u64 whole = together ? CopyShared(low, high - low) : 0;
    for (u32 i = 0; i < Scene::UiAttributes; i++) {
        if ((inputs >> i & 1) == 0) {
            continue;
        }
        const u64 copied = together ? (whole != 0 ? whole + (buffers[i].base - low) : 0)
                                    : CopyShared(buffers[i].base, InputBytes(buffers[i], vertices));
        if (copied == 0) {
            return false;
        }
        const u32 elements = std::min(buffers[i].elements, std::max(vertices, 1u));
        draw.attributes[i] = {
            .address = copied,
            .stride = (buffers[i].stride & 0xffff) | std::min<u32>(elements, 0xffff) << 16,
            .format = buffers[i].word3,
        };
    }
    return true;
}

u32 Writer::TextureHandle(const u32* sharp, bool* linear) {
    const u64 base = SharpAddress(sharp);
    if (base == 0) {
        return 0;
    }
    for (u32 i = 0; i < g_target_count; i++) {
        if (g_targets[i].base == base) {
            return Scene::UiTargetHandles + i;
        }
    }
    bool dynamic = false;
    const u32 handle = Textures::GetBySharp(sharp, dynamic);
    if (linear != nullptr) {
        *linear = dynamic;
    }
    if (handle == 0) {
        ui.unknown_textures++;
        if (g_logged_textures < LoggedUnknowns &&
            std::find(g_unknown_textures.begin(), g_unknown_textures.end(), base) ==
                g_unknown_textures.end()) {
            g_unknown_textures[g_logged_textures++] = base;
            const std::span words{sharp, 8};
            LOG_INFO(Render_Vulkan, "GR2port UI: texture at {:#x} not ready: T# {:x}", base,
                     fmt::join(words, " "));
        }
        return NotReady;
    }
    const auto listed = g_dynamic.begin() + ui.dynamic;
    if (dynamic && ui.dynamic < g_dynamic.size() &&
        std::find(g_dynamic.begin(), listed, handle) == listed) {
        g_dynamic[ui.dynamic++] = handle;
    }
    return handle;
}

void Writer::AddRun(const Scene::UiRun& state) {
    if (ui.runs != 0) {
        Scene::UiRun& last = g_runs[ui.runs - 1];
        if (last.target == state.target && last.vertex == state.vertex &&
            last.pixel == state.pixel && last.blend == state.blend &&
            last.topology == state.topology && last.kept == state.kept &&
            std::equal(std::begin(last.viewport), std::end(last.viewport),
                       std::begin(state.viewport))) {
            last.count++;
            return;
        }
    }
    g_runs[ui.runs++] = state;
}

// A draw of the screen drawing, as UiItems read it from the game's draw items.
void Writer::Draw(const UiItems::Draw& item) {
    const u32 vertex_index = static_cast<u32>(item.vertex);
    const u32 pixel_index = static_cast<u32>(item.pixel);
    if (item.vertex >= Scene::UiVertexProgram::Count ||
        item.pixel >= Scene::UiPixelProgram::Count) {
        ui.unported++;
        if (g_logged_programs < LoggedUnknowns) {
            g_logged_programs++;
            LOG_INFO(Render_Vulkan, "GR2port UI: programs not ported: vs {} ps {} of item {:#x}",
                     vertex_index, pixel_index, GameAddress(item.item));
        }
        return;
    }
    const VertexProgram& vertex_program = VertexPrograms[vertex_index];
    const PixelProgram& pixel_program = PixelPrograms[pixel_index];
    const auto drop = [&](const char* why) {
        ui.dropped++;
        if (g_logged_drops < LoggedUnknowns) {
            g_logged_drops++;
            LOG_INFO(Render_Vulkan, "GR2port UI: draw not drawn, {}: vs {} ps {} of item {:#x}",
                     why, vertex_index, pixel_index, GameAddress(item.item));
        }
    };
    const u32 primitive = item.topology;
    if (ui.draws == Scene::MaxUiDraws || ui.runs == Scene::MaxUiRuns ||
        (primitive != Scene::UiPrimitiveList && primitive != Scene::UiPrimitiveStrip)) {
        ui.dropped++;
        if (g_logged_primitives < LoggedUnknowns && ui.runs != Scene::MaxUiRuns &&
            ui.draws != Scene::MaxUiDraws) {
            g_logged_primitives++;
            LOG_INFO(Render_Vulkan, "GR2port UI: primitive {} not drawn: vs {} ps {}", primitive,
                     vertex_index, pixel_index);
        }
        return;
    }
    const auto [scale_x, offset_x, scale_y, offset_y] = item.viewport;
    const u32 target = item.target != 0 ? ObjectTarget(item.target) : Scene::MaxUiTargets;
    if (target == Scene::MaxUiTargets) {
        drop("no target");
        return;
    }

    const u32 count = item.count;
    // The vertices the draw reads: from first_vertex on, vertices of them.
    u32 first_vertex = 0;
    u32 vertices = 0;
    Scene::UiDraw draw{};
    draw.flags = item.flags;
    if (item.indices == 0) {
        vertices = count;
    } else {
        draw.index_bits = item.index_bits;
        u32 highest = 0;
        draw.indices =
            CopyIndices(item.indices, item.first, count, item.index_bits, first_vertex, highest);
        if (draw.indices == 0) {
            drop("no indices");
            return;
        }
        vertices = highest - first_vertex + 1;
    }

    if (vertex_program.inputs != 0) {
        std::array<Buffer, Scene::UiAttributes> buffers{};
        for (u32 i = 0; i < Scene::UiAttributes; i++) {
            const UiItems::Input& input = item.inputs[i];
            buffers[i] = {.base = input.address,
                          .stride = input.stride,
                          .elements = input.elements,
                          .word3 = input.format};
        }
        if (!CopyInputs(buffers, vertex_program.inputs, first_vertex, vertices, draw)) {
            drop("no inputs");
            return;
        }
    }
    if (vertex_program.constants) {
        draw.vs_constants = Copy(item.vs_constants, item.vs_bytes);
        if (draw.vs_constants == 0) {
            drop("no vertex constants");
            return;
        }
        // The Transform and Skinned programs place a vertex by the 4x4 matrix at the start of their
        // constants, column by column, and never read its third row.
        const bool transform = item.vertex == Scene::UiVertexProgram::Transform ||
                               item.vertex == Scene::UiVertexProgram::TransformB;
        if (transform || item.vertex == Scene::UiVertexProgram::Skinned ||
            item.vertex == Scene::UiVertexProgram::SkinnedB) {
            const auto matrix = Load<std::array<float, 16>>(item.vs_constants);
            if (transform && matrix[2] == 0.0f && matrix[6] == 0.0f && matrix[10] == 0.0f &&
                matrix[14] == 0.0f) {
                draw.flags |= Scene::UiDrawNoDepth;
            }
            if (matrix[14] == MarkerFit::DrawTag) {
                draw.flags |= Scene::UiDrawMarker;
            }
        }
    }
    if (vertex_program.palette) {
        draw.vs_palette = Copy(item.vs_palette, item.palette_bytes);
        if (draw.vs_palette == 0) {
            drop("no palette");
            return;
        }
        draw.palette_rows = item.palette_bytes / 16;
    }
    std::array<u32, 8> sharp{};
    if (pixel_program.textures != 0) {
        if (item.textures[0].sharp == 0) {
            drop("no texture");
            return;
        }
        sharp = Load<std::array<u32, 8>>(item.textures[0].sharp);
        bool linear = false;
        draw.textures[0] = TextureHandle(sharp.data(), &linear);
        draw.samplers[0] = SamplerOf(Load<std::array<u32, 4>>(item.textures[0].sampler).data());
        // The game writes text into linear images; movies and photos are linear too.
        if (linear && item.pixel != Scene::UiPixelProgram::Movie &&
            item.pixel != Scene::UiPixelProgram::Photo) {
            draw.flags |= Scene::UiDrawText;
            if (item.vertex == Scene::UiVertexProgram::Transform ||
                item.vertex == Scene::UiVertexProgram::TransformB) {
                draw.flags |= Scene::UiDrawAnchored;
            }
        }
        if (item.pixel == Scene::UiPixelProgram::Movie) {
            g_movie_targets |= 1u << target;
        }
        // Menus draw movies as sprites; a movie played on the screen is copied over the picture.
        const u32 source = draw.textures[0] - Scene::UiTargetHandles;
        if (item.vertex == Scene::UiVertexProgram::Master &&
            item.pixel == Scene::UiPixelProgram::Copy && target == ui.picture &&
            draw.textures[0] >= Scene::UiTargetHandles && source < Scene::MaxUiTargets &&
            (g_movie_targets >> source & 1) != 0) {
            draw.flags |= Scene::UiDrawMovie;
        }
        if (item.vertex == Scene::UiVertexProgram::Transform &&
            item.pixel == Scene::UiPixelProgram::Sprite && target == ui.picture) {
            const FinderTexture shape{(sharp[2] & 0x3fff) + 1, (sharp[2] >> 14 & 0x3fff) + 1,
                                      sharp[1] >> 20 & 0x3f};
            g_finder_frame = g_finder_frame || shape == FinderGuide;
            if ((g_finder_frame || g_finder_last) &&
                std::ranges::any_of(FinderTextures,
                                    [&](const FinderTexture& known) { return known == shape; })) {
                draw.flags |= Scene::UiDrawFinder;
            }
        }
    }
    if (pixel_program.textures == 2) {
        if (item.textures[1].sharp == 0) {
            drop("no second texture");
            return;
        }
        draw.textures[1] = TextureHandle(Load<std::array<u32, 8>>(item.textures[1].sharp).data());
        draw.samplers[1] = SamplerOf(Load<std::array<u32, 4>>(item.textures[1].sampler).data());
    }
    if (pixel_program.textures == 2 || pixel_program.constants) {
        draw.ps_constants = Copy(item.ps_constants, item.ps_bytes);
        if (draw.ps_constants == 0) {
            drop("no pixel constants");
            return;
        }
    }
    if (item.pixel == Scene::UiPixelProgram::Photo) {
        LOG_INFO(Render_Vulkan,
                 "GR2port photo: drawn into target {}, the frame from {:#x} as {:#x}, the filter's "
                 "frame as {:#x}, curves of {} bytes at {:#x}; world target {}",
                 target, SharpAddress(sharp.data()), draw.textures[0], draw.textures[1],
                 item.ps_bytes, draw.ps_constants, ui.world);
    }
    // A texture the renderer does not have yet reads white, which a movie's colour conversion
    // turns pink, so the draw is left out until the renderer has it.
    if (draw.textures[0] == NotReady || draw.textures[1] == NotReady) {
        return;
    }
    // The filtered view goes under the viewfinder's first piece.
    if ((draw.flags & Scene::UiDrawFinder) != 0 && !filtered) {
        filtered = true;
        Filter(target);
    }

    g_frame_writes |= 1u << target;
    AddRun({
        .target = target,
        .vertex = vertex_index,
        .pixel = pixel_index,
        .blend = item.blend,
        .topology = primitive,
        .viewport = {offset_x - scale_x, offset_y - scale_y, 2.0f * scale_x, 2.0f * scale_y},
        .first = ui.draws,
        .count = 1,
        .kept = item.kept ? 1u : 0u,
    });
    g_commands[ui.draws] = {
        .vertex_count = count,
        .instance_count = 1,
        .first_vertex = 0,
        .first_instance = ui.draws,
    };
    std::memcpy(packet + Scene::UiDrawsOffset + u64{ui.draws} * sizeof(draw), &draw, sizeof(draw));
    ui.draws++;
}

// Draws the picked filter over the part of target the photo takes, as the shot draws it into the
// photo, so the viewfinder shows what the photo will hold. Leaves room for one more draw.
void Writer::Filter(u32 target) {
    const u32 filter = PhotoUi::Filter();
    if (filter >= PhotoUi::Filters || target >= g_target_count ||
        ui.draws + 1 >= Scene::MaxUiDraws || ui.runs + 1 >= Scene::MaxUiRuns) {
        return;
    }
    const VAddr film = Load<VAddr>(g_base + FilmFrame - Hooks::CodeBase);
    if (film == 0 || Load<u64>(film + FilmMemory) == 0 || Load<s32>(film + FilmState) >= 0) {
        return;
    }
    Scene::UiDraw draw{};
    draw.flags = Scene::UiDrawPreview | Scene::UiDrawFinder;
    // The shot's frame holds the world only in the frame of a shot. Between shots the world goes
    // into the picture, which this draw writes, so it reads a copy of the picture.
    draw.textures[0] = Scene::UiCopyHandle;
    draw.samplers[0] = PreviewSampler;
    const auto film_sharp = Load<std::array<u32, 8>>(film + FilmSharp);
    draw.textures[1] = TextureHandle(film_sharp.data());
    if (draw.textures[1] == 0 || draw.textures[1] == NotReady) {
        return;
    }
    draw.samplers[1] = SamplerOf(Load<std::array<u32, 4>>(film + FilmSampler).data());

    // The shot's rectangle on the viewfinder's window, the middle PhotoSide x PhotoSide of the
    // canvas. As a piece of the viewfinder it goes where the window goes at every aspect, which is
    // where the photo's coordinates lie in the picture.
    auto vertices =
        Load<std::array<float, PhotoVertices * PhotoFloats>>(g_base + PhotoMesh - Hooks::CodeBase);
    for (u32 i = 0; i < PhotoVertices; i++) {
        float* const vertex = vertices.data() + i * PhotoFloats;
        vertex[0] *= PhotoSide / CanvasWidth;
        vertex[1] *= PhotoSide / CanvasHeight;
    }
    const u64 data = Copy(reinterpret_cast<VAddr>(vertices.data()), sizeof(vertices));
    draw.vs_constants = Copy(reinterpret_cast<VAddr>(MasterColor.data()), sizeof(MasterColor));
    draw.ps_constants = Copy(
        g_base + FilterObjects - Hooks::CodeBase + filter * FilterBytes + FilterCurves, CurveBytes);
    if (data == 0 || draw.vs_constants == 0 || draw.ps_constants == 0) {
        return;
    }
    constexpr u32 Stride = PhotoFloats * sizeof(float);
    draw.attributes[0] = {.address = data, .stride = Stride, .format = ShapePosition};
    draw.attributes[1] = {
        .address = data + 3 * sizeof(float), .stride = Stride, .format = ShapeCoordinate};
    draw.attributes[2] = {
        .address = data + 5 * sizeof(float), .stride = Stride, .format = ShapeCoordinate};
    draw.attributes[3] = {
        .address = data + 7 * sizeof(float), .stride = Stride, .format = ShapeColor};
    const float width = static_cast<float>(g_targets[target].width);
    const float height = static_cast<float>(g_targets[target].height);
    // Kept: Hide UI hides the screen drawing over the picture, not the picture.
    AddRun({
        .target = target,
        .vertex = static_cast<u32>(Scene::UiVertexProgram::Photo),
        .pixel = static_cast<u32>(Scene::UiPixelProgram::Photo),
        .blend = 0,
        .topology = Scene::UiPrimitiveStrip,
        .viewport = {0.0f, height, width, -height},
        .first = ui.draws,
        .count = 1,
        .kept = 1,
    });
    ui.copy = ui.runs;
    g_commands[ui.draws] = {
        .vertex_count = PhotoVertices,
        .instance_count = 1,
        .first_vertex = 0,
        .first_instance = ui.draws,
    };
    std::memcpy(packet + Scene::UiDrawsOffset + u64{ui.draws} * sizeof(draw), &draw, sizeof(draw));
    ui.draws++;
}

// Draws triangles of ShapeFloats floats a vertex over the whole of a target, after the game's own
// draws.
void Writer::Shape(std::span<const float> vertices, u32 target) {
    const u32 count = static_cast<u32>(vertices.size() / ShapeFloats);
    if (count == 0 || target >= g_target_count || ui.draws == Scene::MaxUiDraws ||
        ui.runs == Scene::MaxUiRuns) {
        return;
    }
    const u64 data = Copy(reinterpret_cast<VAddr>(vertices.data()), vertices.size_bytes());
    if (data == 0) {
        return;
    }
    constexpr u32 Stride = ShapeFloats * sizeof(float);
    Scene::UiDraw draw{};
    draw.attributes[0] = {.address = data, .stride = Stride, .format = ShapePosition};
    draw.attributes[1] = {
        .address = data + 3 * sizeof(float), .stride = Stride, .format = ShapeCoordinate};
    draw.attributes[2] = {
        .address = data + 5 * sizeof(float), .stride = Stride, .format = ShapeColor};
    const float width = static_cast<float>(g_targets[target].width);
    const float height = static_cast<float>(g_targets[target].height);
    g_frame_writes |= 1u << target;
    AddRun({
        .target = target,
        .vertex = static_cast<u32>(Scene::UiVertexProgram::Pass),
        .pixel = static_cast<u32>(Scene::UiPixelProgram::Color),
        .blend = PremultipliedBlend,
        .topology = Scene::UiPrimitiveList,
        .viewport = {0.0f, height, width, -height},
        .first = ui.draws,
        .count = 1,
        .kept = 0,
    });
    g_commands[ui.draws] = {
        .vertex_count = count,
        .instance_count = 1,
        .first_vertex = 0,
        .first_instance = ui.draws,
    };
    std::memcpy(packet + Scene::UiDrawsOffset + u64{ui.draws} * sizeof(draw), &draw, sizeof(draw));
    ui.draws++;
}

void Writer::Finish() {
    ui.targets = g_target_count;
    std::memcpy(packet + Scene::UiTargetsOffset, g_targets.data(),
                g_target_count * sizeof(Scene::UiTarget));
    std::memcpy(packet + Scene::UiRunsOffset, g_runs.data(), ui.runs * sizeof(Scene::UiRun));
    std::memcpy(packet + Scene::UiCommandsOffset, g_commands.data(),
                ui.draws * sizeof(Scene::VertexCommand));
    std::memcpy(packet + Scene::UiDynamicOffset, g_dynamic.data(), ui.dynamic * sizeof(u32));
}

} // namespace

void Install(VAddr module_base) {
    g_base = module_base;
    for (const Hooks::Entry& entry : Writers) {
        if (!Hooks::Hooked(Hooks::Return(module_base, entry))) {
            LOG_ERROR(Render_Vulkan, "GR2port: the function at {:#x} is not the expected code",
                      entry.address);
        }
    }
}

void SetScrollBar(const ScrollBar& bar) {
    const std::scoped_lock lock{g_bar_mutex};
    g_bar = bar;
    g_bar_set = std::chrono::steady_clock::now();
}

void OnFrameGraphEnd() {
    g_frame_graph = true;
}

void Skip() {
    g_frame_graph = false;
    UiItems::Clear();
    PhotoUi::ForgetItems(Load<u32>(g_base + PreviousContextIndex - Hooks::CodeBase));
}

namespace {

// Adds an upright bar with rounded ends, in canvas pixels, as triangles around its middle. A
// canvas pixel is tall times as high as it is wide, and the ends are round on the picture.
void AddBar(std::vector<float>& vertices, float x, float top, float bottom, float width, float tall,
            const std::array<float, 4>& color, float opacity) {
    const float radius = 0.5f * width;
    const float end = radius / tall;
    const float upper = std::min(top + end, 0.5f * (top + bottom));
    const float lower = std::max(bottom - end, 0.5f * (top + bottom));
    std::array<std::array<float, 2>, 2 * (EndSteps + 1)> outline;
    for (u32 step = 0; step <= EndSteps; step++) {
        const float angle = std::numbers::pi_v<float> * static_cast<float>(step) / EndSteps;
        const float across = radius * std::cos(angle);
        const float along = end * std::sin(angle);
        outline[step] = {x - across, upper - along};
        outline[EndSteps + 1 + step] = {x + across, lower + along};
    }
    const auto add = [&](float canvas_x, float canvas_y) {
        vertices.insert(vertices.end(), {2.0f * canvas_x / CanvasWidth - 1.0f,
                                         1.0f - 2.0f * canvas_y / CanvasHeight, 0.0f, 0.0f, 0.0f,
                                         color[0], color[1], color[2], color[3] * opacity});
    };
    for (size_t i = 0; i < outline.size(); i++) {
        const auto& next = outline[(i + 1) % outline.size()];
        add(x, 0.5f * (upper + lower));
        add(outline[i][0], outline[i][1]);
        add(next[0], next[1]);
    }
}

// Ends the picture's draws with the scroll bar, when one is set.
void AddScrollBar(Writer& writer, u32 picture) {
    ScrollBar bar;
    {
        const std::scoped_lock lock{g_bar_mutex};
        if (std::chrono::steady_clock::now() - g_bar_set > BarLife) {
            return;
        }
        bar = g_bar;
    }
    if (bar.opacity <= 0.0f || bar.bottom <= bar.top || bar.width <= 0.0f ||
        picture >= g_target_count || g_targets[picture].width == 0 ||
        g_targets[picture].height == 0) {
        return;
    }
    // The canvas fills the picture, whatever the picture's shape, unless the screen drawing keeps
    // its 16:9 shape.
    const float tall = Ui::FitsScreen()
                           ? 1.0f
                           : static_cast<float>(g_targets[picture].height) / CanvasHeight /
                                 (static_cast<float>(g_targets[picture].width) / CanvasWidth);
    static std::vector<float> vertices;
    vertices.clear();
    AddBar(vertices, bar.x, bar.top, bar.bottom, bar.width, tall, TrackColor, bar.opacity);
    AddBar(vertices, bar.x, bar.top, bar.bottom, bar.width * LineShare, tall, LineColor,
           bar.opacity);
    AddBar(vertices, bar.x, bar.thumb_top, bar.thumb_bottom, bar.width, tall, ThumbColor,
           bar.opacity);
    writer.Shape(vertices, picture);
}

// Marks the targets other than back buffers that the frame writes after a while without writes.
void MarkTargets(Scene::Header& header) {
    const u64 frame = header.frame;
    u32 fresh = 0;
    for (u32 t = 0; t < Scene::MaxUiTargets; t++) {
        const u32 bit = 1u << t;
        if ((g_frame_writes & bit) == 0) {
            continue;
        }
        if ((g_back_buffers & bit) == 0 &&
            (g_written[t] == 0 || frame - (g_written[t] - 1) > FreshGap)) {
            fresh |= bit;
        }
        g_written[t] = frame + 1;
    }
    header.ui.fresh_targets = fresh;
}

} // namespace

void Write(u8* packet, u64 address, Scene::Header& header, VAddr world) {
    const auto start = std::chrono::steady_clock::now();
    const u32 index = Load<u32>(g_base + PreviousContextIndex - Hooks::CodeBase);
    g_world_frames += g_frame_graph ? 1 : 0;
    g_frame_graph = false;
    PhotoUi::ForgetItems(index);

    Scene::UiHeader& ui = header.ui;
    const u32 picture =
        ObjectTarget(g_base + BackBuffers - Hooks::CodeBase + u64{index} * BackBufferBytes);
    ui.picture = picture == Scene::MaxUiTargets ? 0 : picture;
    const u32 world_target = world != 0 ? ObjectTarget(world) : ui.picture;
    ui.world = world_target == Scene::MaxUiTargets ? ui.picture : world_target;
    // The world's picture goes into its target before the screen drawing.
    g_frame_writes = ui.only == 0 ? 1u << ui.world : 0;
    g_finder_last = g_finder_frame;
    g_finder_frame = false;
    g_back_buffers |= picture != Scene::MaxUiTargets ? 1u << picture : 0;
    Writer writer{packet, address, ui};
    for (const UiItems::Draw& item : UiItems::Draws()) {
        writer.Draw(item);
    }
    UiItems::Clear();
    if (picture != Scene::MaxUiTargets) {
        AddScrollBar(writer, picture);
    }
    writer.Finish();
    ui.photo = PhotoTarget(ui);
    ui.hidden = PhotoUi::Hidden() ? 1 : 0;
    MarkTargets(header);

    g_draws += ui.draws;
    g_runs_total += ui.runs;
    g_bytes += writer.Used();
    g_unknown += ui.unknown_textures;
    g_unported += ui.unported;
    g_dropped += ui.dropped;
    g_write_ns += static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       std::chrono::steady_clock::now() - start)
                                       .count());
    if (++g_frames == ReportFrames) {
        constexpr double Frames = ReportFrames;
        LOG_INFO(Render_Vulkan,
                 "GR2port UI: per frame {:.1f} draws in {:.1f} runs, {:.0f} bytes copied, {:.1f} "
                 "us to write; {:.1f} reads of textures not ready, {:.1f} draws not ported, {:.1f} "
                 "dropped; {} with the frame graph, {} targets",
                 static_cast<double>(g_draws) / Frames, static_cast<double>(g_runs_total) / Frames,
                 static_cast<double>(g_bytes) / Frames,
                 static_cast<double>(g_write_ns) * 1e-3 / Frames,
                 static_cast<double>(g_unknown) / Frames, static_cast<double>(g_unported) / Frames,
                 static_cast<double>(g_dropped) / Frames, g_world_frames, g_target_count);
        g_frames = 0;
        g_world_frames = 0;
        g_draws = 0;
        g_runs_total = 0;
        g_bytes = 0;
        g_unknown = 0;
        g_unported = 0;
        g_dropped = 0;
        g_write_ns = 0;
    }
}

} // namespace Gr2Port::UiCapture
