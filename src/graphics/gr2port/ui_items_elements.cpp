// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include "common/logging/log.h"
#include "graphics/gr2port/ui_items_internal.h"

// The layout elements: FUN_013eadb0 makes, per submesh of a UI model, a bind item that binds the
// vertex and index buffers and holds the element's matrix and bone palette, then an element item
// per material and range of indices, whose function FUN_013ecd20 draws them through
// FUN_013e7300. The readers here build the same draws from the items, the bind item and the
// material: programs from the material's shader objects, inputs from the vertex buffer objects,
// constants as the shader objects' binders build them (FUN_017b59b0, FUN_017b6a40), textures as
// the texture binder FUN_017b8040 binds them, and the blend of FUN_01509d90.
namespace Gr2Port::UiItems {

namespace {

constexpr VAddr BindItem = 0x1aeb430;
constexpr VAddr ElementItem = 0x1aeb458;

// The element item: its material, first index and index count, the bind item's matrix, the comic
// mode byte and value, the bind item's bone count, and the material's constants it holds from
// + 0x50: the colour from + 0x50, the pixel colour from + 0x5c, the UV transform at + 0x90, the
// colour matrix's weight at + 0xa0, the comic range at + 0xa4 and the mask range at + 0xac.
constexpr u64 ElementMaterial = 0x18;
constexpr u64 ElementFirst = 0x20;
constexpr u64 ElementCount = 0x24;
constexpr u64 ElementMatrix = 0x28;
constexpr u64 ElementComic = 0x30;
constexpr u64 ElementComicValue = 0x34;
constexpr u64 ElementBones = 0x40;
constexpr u64 ElementColor = 0x50;
constexpr u64 ElementPixelColor = 0x5c;
constexpr u64 ElementUv = 0x90;
constexpr u64 ElementMatrixWeight = 0xa0;
constexpr u64 ElementComicTop = 0xa4;
constexpr u64 ElementComicBottom = 0xa8;
constexpr u64 ElementMaskTop = 0xac;
constexpr u64 ElementMaskBottom = 0xb0;
// The bind item's buffer sets of vertices, indices and skin streams, before its matrix.
constexpr u64 MatrixVertices = 0x18;
constexpr u64 MatrixIndices = 0x10;
constexpr u64 MatrixSkin = 0x8;
constexpr u32 MatrixBytes = 64;
// A buffer set: its object, and its kind, 1 for indices.
constexpr u64 SetKind = 0x10;
constexpr s32 IndexKind = 1;
// A vertex buffer object: its attributes' count, and per attribute its semantic (a byte of a
// 4-byte entry) and its V#.
constexpr u64 VertexCount = 0x1c;
constexpr u64 VertexSemantic = 0x37;
constexpr u64 VertexSharp = 0x64;
// An index object: its buffer, whose first word is the indices' address, and its type, 0 for
// 16-bit indices.
constexpr u64 IndexType = 0x18;
// The bone count (u32) and, 8 bytes on, the palette of bone count x 64 bytes.
constexpr u64 BonePalette = 0x8;
constexpr u32 BoneBytes = 64;

// A material: its definition (+ 0x10 its type, + 0x28 for type 3 whether it has a texture), its
// vertex and pixel shader objects, its blend mode, its colour matrix and its two texture
// bindings, of the texture and of the mask.
constexpr u64 DefType = 0x10;
constexpr u64 DefTextured = 0x28;
constexpr u64 MaterialVertex = 0x18;
constexpr u64 MaterialGeometry = 0x20;
constexpr u64 MaterialPixel = 0x28;
constexpr u64 MaterialBlend = 0x38;
constexpr u64 MaterialColorMatrix = 0x40;
constexpr u64 MaterialTexture = 0xc0;
constexpr u64 MaterialMask = 0xc8;

// A pixel shader object's constant builder, its virtual function at + 0x38 (which + 0x18 calls):
// the Color family's, FUN_017b6e00, binds no colour.
constexpr u64 ObjectConstants = 0x38;
constexpr VAddr ColorlessConstants = 0x17b6e00;
// The element programs' shader objects, by slot, as FUN_0119d380 makes them: the stroke's
// vertex object is slot 5 of the vertex objects, its pixel object slot 14 of the pixel objects.
constexpr VAddr VertexObjects = 0x1efbee0;
constexpr VAddr PixelObjects = 0x1efbf20;
constexpr s32 StrokeVertexSlot = 5;
constexpr s32 StrokePixelSlot = 14;
// A stroke definition's values at + 0x24 .. + 0x38, which FUN_013e7300 gives the geometry and
// pixel programs, the first, third and fifth of them scaled.
constexpr u64 StrokeValues = 0x24;
// A geometry object's program at + 0xa0, whose u16 at + 6 must be 0 for FUN_016173f0 to draw
// with the geometry stage on.
constexpr u64 GeometryProgram = 0xa0;
constexpr u64 ProgramDrawsAt = 0x6;
// A vertex shader object's configurations, plain and with bones, and a configuration's inputs:
// their count, their semantics, and their resource table, whose 16-byte entries hold the slot
// in their fourth byte.
constexpr u64 PlainConfig = 0x10;
constexpr u64 SkinnedConfig = 0x288;
constexpr u64 ConfigInputs = 0x28;
constexpr u64 ConfigSemantics = 0x268;
constexpr u64 ConfigResources = 0x58;
constexpr u32 Semantics = 16;

// The texture bindings' classes and how each gives its texture object.
constexpr VAddr PackTexture = 0x1aeb3d0;
constexpr VAddr StreamedTexture = 0x1aeb400;
constexpr VAddr MovieTexture = 0x1aeb380;
constexpr VAddr FontTexture = 0x1aeb3a8;
constexpr u64 ResourceState = 0x20;
constexpr s32 ResourceReady = 4;
constexpr u64 ResourceTexture = 0xb0;
constexpr u64 FontTextureObject = 0xa8;
constexpr u64 MovieTextureObject = 0x50;

// The texture the binder takes where there is none, and the comic numbers' target.
constexpr VAddr DefaultTexture = 0x1f58600;
constexpr VAddr ComicTarget = 0x1efc058;
// A texture object: + 8 its memory, + 0x3c whose sign bit says it may be sampled, + 0x30 the T#,
// + 0x50 the S#.
constexpr u64 TextureMemory = 0x8;
constexpr u64 TextureSampleable = 0x3c;
constexpr u64 TextureSharp = 0x30;
constexpr u64 TextureSampler = 0x50;
// The size of the last pass target, (w, h, 1/w, 1/h), which comic constants read.
constexpr VAddr PassTargetSize = 0x1efc5d0;

// The colours FUN_013e7300 gives the vertex program for a type 3 material without a texture, and
// for a material whose texture binding gives none.
constexpr VAddr UntexturedColor = 0x18b4c40;
constexpr VAddr MissingColor = 0x18b4c50;

// A movie material (type 1): the item's movie (-1 without one), and in its pixel colour's place
// whether the movie is converted (below 0), whether this frame converts it (1), and its size. The
// vertex colour without a movie; the screen target, which is never converted; the target a
// 1920 x 1620 movie is converted into, and the one of the last other size with that size.
constexpr u64 ElementMovie = 0x38;
constexpr VAddr MovieColor = 0x18b5e70;
constexpr VAddr ScreenTarget = 0x1f26358;
constexpr VAddr FullMovieTarget = 0x1f25d18;
constexpr VAddr MovieTarget = 0x1efbc78;
constexpr u16 FullMovieWidth = 1920;
constexpr u16 FullMovieHeight = 1620;
// The target the conversion leaves bound: the back buffer, or the screen target while the pool's
// flag is set.
constexpr VAddr BackBuffer = 0x1f315a8;
constexpr VAddr ScreenTargetFlags = 0x1f26c90;
constexpr VAddr FilledPool = 0x1f21bac;
constexpr u64 TargetWidth = 0x14;
constexpr u64 TargetHeight = 0x16;

constexpr std::array<float, 4> White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr std::array<float, 4> PlainUv{1.0f, 0.0f, 0.5f, 0.5f};
constexpr std::array<float, 4> NoPixelColor{1.0f, 0.0f, 0.0f, 0.0f};

std::vector<VAddr> g_logged;

// A movie's conversion and the element's draw, added once the function made the target.
struct Conversion {
    bool pending = false;
    bool full = false;
    Draw convert;
    Draw element;
};
Conversion g_conversion;

void LogOnce(const char* what, VAddr value) {
    if (g_logged.size() < 64 && std::ranges::find(g_logged, value) == g_logged.end()) {
        g_logged.push_back(value);
        LOG_INFO(Render_Vulkan, "GR2port UI: element {} {:#x} not read", what, value);
    }
}

// The texture object a material's texture binding gives (its function at + 0x10), or 0.
VAddr TextureOf(VAddr binding) {
    if (binding == 0) {
        return 0;
    }
    const VAddr vtable = Load<VAddr>(binding);
    const auto ready = [](VAddr resource) {
        return resource != 0 && Load<s32>(resource + ResourceState) == ResourceReady
                   ? resource + ResourceTexture
                   : 0;
    };
    if (vtable == Game(PackTexture)) {
        return ready(Load<VAddr>(binding + 0x8));
    }
    if (vtable == Game(StreamedTexture)) {
        const VAddr streamed = ready(Load<VAddr>(binding + 0x10));
        return streamed != 0 ? streamed : ready(Load<VAddr>(binding + 0x8));
    }
    if (vtable == Game(MovieTexture)) {
        return Load<VAddr>(binding + MovieTextureObject);
    }
    if (vtable == Game(FontTexture)) {
        const VAddr font = Load<VAddr>(binding + 0x8);
        return font != 0 ? font + FontTextureObject : Game(DefaultTexture);
    }
    LogOnce("texture binding", vtable - g_base + Hooks::CodeBase);
    return 0;
}

// Whether the binder binds a texture object: it has memory and may be sampled.
bool Bindable(VAddr texture) {
    return texture != 0 && Load<u64>(texture + TextureMemory) != 0 &&
           Load<s32>(texture + TextureSampleable) < 0;
}

Texture TextureWords(VAddr texture) {
    return {.sharp = texture + TextureSharp, .sampler = texture + TextureSampler};
}

// The slot of a shader object in a table of 8-byte entries, or -1.
s32 SlotOf(VAddr table, u32 slots, VAddr object) {
    for (u32 slot = 0; slot < slots; slot++) {
        if (Load<VAddr>(Game(table) + slot * sizeof(VAddr)) == object) {
            return static_cast<s32>(slot);
        }
    }
    return -1;
}

Scene::UiVertexProgram VertexProgram(VAddr object, bool bones) {
    using enum Scene::UiVertexProgram;
    switch (SlotOf(VertexObjects, 8, object)) {
    case 0:
        return bones ? Skinned : Transform;
    case 1:
        return bones ? SkinnedB : TransformB;
    default:
        return Count;
    }
}

// The pixel program of a pixel shader object's variant: its row by the comic values (0 without,
// 1 testing, 2 writing) and its column by the colour matrix. With the matrix, only the sprite's
// first row is ported (FUN_017b68f0 loads 0x1b77944 at + 0x178).
Scene::UiPixelProgram PixelProgram(VAddr object, u32 row, bool matrix) {
    using enum Scene::UiPixelProgram;
    const s32 slot = SlotOf(PixelObjects, 16, object);
    if (matrix) {
        return slot == 0 && row == 0 ? SpriteMatrix : Count;
    }
    switch (slot) {
    case 0:
        return row == 0 ? Sprite : row == 1 ? MaskSprite : Count;
    case 1:
        return row == 0 ? Color : row == 2 ? Mask : Count;
    case 3:
        return row == 0 ? Frame : Count;
    case 4:
        return row == 0 ? Masked : Count;
    default:
        return Count;
    }
}

// The vertex inputs the configuration reads, as the engine's commit FUN_01614150 binds them from
// the vertex buffer objects the bind item's sets hold, by semantic.
void ReadInputs(VAddr config, VAddr matrix, Draw& draw) {
    std::array<VAddr, Semantics> sharps{};
    const VAddr vertex_set = Load<VAddr>(matrix - MatrixVertices);
    if (vertex_set == 0 || Load<s32>(vertex_set + SetKind) == IndexKind) {
        return;
    }
    const VAddr skin_set = Load<VAddr>(matrix - MatrixSkin);
    if (skin_set != 0 && Load<s32>(skin_set + SetKind) == IndexKind) {
        return;
    }
    const auto note = [&sharps](VAddr object) {
        if (object == 0) {
            return;
        }
        const u32 count = Load<u16>(object + VertexCount);
        for (u32 i = 0; i < count; i++) {
            const u32 semantic = Load<u8>(object + VertexSemantic + i * 4);
            if (semantic < Semantics) {
                sharps[semantic] = object + VertexSharp + i * 16;
            }
        }
    };
    note(Load<VAddr>(vertex_set));
    note(skin_set != 0 ? Load<VAddr>(skin_set) : 0);
    const u32 inputs = Load<u8>(config + ConfigInputs);
    const VAddr resources = Load<VAddr>(config + ConfigResources);
    for (u32 i = 0; i < inputs; i++) {
        const u32 semantic = Load<u8>(config + ConfigSemantics + i);
        const u32 slot = Load<u8>(resources + 3 + i * 16);
        if (semantic >= Semantics || sharps[semantic] == 0 || slot >= Scene::UiAttributes) {
            continue;
        }
        const auto words = Load<std::array<u32, 4>>(sharps[semantic]);
        draw.inputs[slot] = {
            .address = u64{words[0]} | u64{words[1] & 0xfff} << 32,
            .stride = words[1] >> 16 & 0x3fff,
            .format = words[3],
            .elements = words[2],
        };
    }
}

// The pixel constants FUN_017b6a40 builds: the colour, or (1, 0, 0, 0); with comic values the
// last pass target's 1 / width and 1 / height, two zeros and the four values; with a mask range
// two zeros, its scale and offset; with a colour matrix its 16 floats, the weight and 1 less it,
// and two zeros.
VAddr PixelConstants(bool colored, const float* color, const float* comic, const float* mask,
                     const float* matrix, float weight, u32& bytes) {
    std::array<float, 48> f{};
    u32 n = 0;
    if (colored) {
        std::memcpy(f.data(), color != nullptr ? color : NoPixelColor.data(), 16);
        n = 4;
    }
    if (comic != nullptr) {
        f[n] = Load<float>(Game(PassTargetSize) + 8);
        f[n + 1] = Load<float>(Game(PassTargetSize) + 12);
        std::memcpy(&f[n + 4], comic, 16);
        n += 8;
    }
    if (mask != nullptr) {
        f[n + 2] = mask[0];
        f[n + 3] = mask[1];
        n += 4;
    }
    if (matrix != nullptr) {
        std::memcpy(&f[n], matrix, 64);
        f[n + 16] = weight;
        f[n + 17] = 1.0f - weight;
        n += 20;
    }
    bytes = n * 4;
    return n != 0 ? Keep(f.data(), bytes) : 0;
}

// FUN_013e7300's comic values from the item's comic mode and value: testing (mode 0) reads the
// comic numbers' target and passes where it holds the value, writing (mode 1) writes the value.
// The pixel programs' row by them: 0 without, 1 testing, 2 writing.
struct Comic {
    std::array<float, 4> values{};
    bool on = false;
    u32 row = 0;
    VAddr texture = 0;
};

Comic ComicOf(VAddr item) {
    Comic comic;
    const float value = Load<float>(item + ElementComicValue);
    if (value == 0.0f) {
        return comic;
    }
    const float scaled = value * 0.007843138f;
    comic.on = true;
    if (Load<u8>(item + ElementComic) == 0) {
        comic.values[0] = scaled + -0.003921569f;
        comic.values[1] = scaled + 0.003921569f;
        comic.values[3] = Load<float>(item + ElementComicBottom);
        const float top = Load<float>(item + ElementComicTop);
        if (top != comic.values[3]) {
            comic.values[2] = 1.0f / (top - comic.values[3]);
            comic.values[3] = -(comic.values[2] * comic.values[3]);
        }
        comic.texture = Game(ComicTarget);
    } else {
        comic.values[0] = scaled;
    }
    comic.row = comic.values[1] != 0.0f ? 1 : 2;
    return comic;
}

void ReadBind(VAddr) {}

// The comic's ink strokes, material type 6. In comic mode 0 the stroke's export program, the vertex
// object's second configuration, and the material's geometry object draw them with the geometry
// stage on (FUN_01616a50(0xb0)): the export program takes the colour, the geometry program the
// matrix and four of the definition's values (FUN_017b5fc0), the pixel program the comic values and
// three more (FUN_017b7f00). In mode 1 the vertex object's first configuration draws them.
void ReadStroke(VAddr item, VAddr material, VAddr def, VAddr matrix) {
    const bool geometry = Load<u8>(item + ElementComic) != 1;
    const Comic comic = ComicOf(item);
    const VAddr bones = Load<VAddr>(item + ElementBones);
    const bool second = geometry || (bones != 0 && Load<u32>(bones) != 0);
    const VAddr gs = Load<VAddr>(material + MaterialGeometry);
    if (geometry &&
        (gs == 0 || Load<u16>(Load<VAddr>(gs + GeometryProgram) + ProgramDrawsAt) != 0)) {
        return;
    }
    const VAddr vertex_object = Load<VAddr>(material + MaterialVertex);
    const VAddr pixel_object = Load<VAddr>(material + MaterialPixel);
    Draw draw{};
    // The vertex object's first configuration holds the Transform program (FUN_017b6370 loads
    // 0x1b11508 there, as FUN_017b60e0 does for the elements'), the second the stroke's export
    // program. The pixel object's rows (FUN_017b7e80): the stroke's program, one not ported, and
    // Mask.
    using Vertex = Scene::UiVertexProgram;
    using Pixel = Scene::UiPixelProgram;
    const bool stroke_object = SlotOf(VertexObjects, 8, vertex_object) == StrokeVertexSlot;
    draw.vertex = !stroke_object ? Vertex::Count
                  : geometry     ? Vertex::Stroke
                  : !second      ? Vertex::Transform
                                 : Vertex::Count;
    const bool rows = SlotOf(PixelObjects, 16, pixel_object) == StrokePixelSlot &&
                      Load<VAddr>(material + MaterialColorMatrix) == 0;
    draw.pixel = !rows            ? Pixel::Count
                 : comic.row == 0 ? Pixel::Stroke
                 : comic.row == 2 ? Pixel::Mask
                                  : Pixel::Count;
    draw.target = Target();
    draw.viewport = Viewport(draw.target);
    s32 mode = Load<s32>(material + MaterialBlend);
    if (static_cast<u32>(mode) >= 0x10000) {
        mode >>= 16;
    }
    draw.blend = Blend(mode);
    draw.topology = Scene::UiPrimitiveList;
    ReadInputs(vertex_object + (second ? SkinnedConfig : PlainConfig), matrix, draw);
    const VAddr index_set = Load<VAddr>(matrix - MatrixIndices);
    if (index_set != 0 && Load<s32>(index_set + SetKind) == IndexKind) {
        const VAddr indices = Load<VAddr>(index_set);
        draw.indices = Load<VAddr>(Load<VAddr>(indices));
        draw.index_bits = Load<s32>(indices + IndexType) == 0 ? 16 : 32;
    }
    draw.first = Load<u32>(item + ElementFirst);
    draw.count = Load<u32>(item + ElementCount);

    // FUN_017b5d60: the colour alone in the second configuration, else as an element's.
    const auto color = Load<std::array<float, 4>>(item + ElementColor);
    if (second) {
        draw.vs_bytes = sizeof(color);
        draw.vs_constants = Keep(color.data(), draw.vs_bytes);
    } else {
        std::array<u8, MatrixBytes + 32> vs{};
        std::memcpy(vs.data(), reinterpret_cast<const void*>(matrix), MatrixBytes);
        std::memcpy(vs.data() + MatrixBytes, color.data(), 16);
        std::memcpy(vs.data() + MatrixBytes + 16, PlainUv.data(), 16);
        draw.vs_bytes = static_cast<u32>(vs.size());
        draw.vs_constants = Keep(vs.data(), draw.vs_bytes);
    }
    const auto values = Load<std::array<float, 6>>(def + StrokeValues);
    if (geometry) {
        std::array<u8, MatrixBytes + 16> gs_constants{};
        std::memcpy(gs_constants.data(), reinterpret_cast<const void*>(matrix), MatrixBytes);
        const std::array<float, 4> widths{values[0] * 0.01f, values[1], values[2] * 0.01f,
                                          values[3]};
        std::memcpy(gs_constants.data() + MatrixBytes, widths.data(), sizeof(widths));
        draw.palette_bytes = static_cast<u32>(gs_constants.size());
        draw.vs_palette = Keep(gs_constants.data(), draw.palette_bytes);
    }
    std::array<float, 11> ps{};
    u32 floats = 0;
    if (comic.on) {
        ps[0] = Load<float>(Game(PassTargetSize) + 8);
        ps[1] = Load<float>(Game(PassTargetSize) + 12);
        std::memcpy(&ps[4], comic.values.data(), sizeof(comic.values));
        floats = 8;
    }
    if (geometry) {
        ps[floats] = values[0] * 0.01f;
        ps[floats + 1] = values[4] * 100.0f;
        ps[floats + 2] = values[5];
        floats += 3;
    }
    if (floats != 0) {
        draw.ps_bytes = floats * 4;
        draw.ps_constants = Keep(ps.data(), draw.ps_bytes);
    }
    Add(draw);
}

void ReadElement(VAddr item) {
    const VAddr material = Load<VAddr>(item + ElementMaterial);
    const VAddr matrix = Load<VAddr>(item + ElementMatrix);
    if (material == 0 || matrix == 0) {
        return;
    }
    const VAddr def = Load<VAddr>(material);
    const u32 type = Load<u32>(def + DefType);
    if (type == 6) {
        ReadStroke(item, material, def, matrix);
        return;
    }
    if (type == 0) {
        LogOnce("material type", type);
        return;
    }
    const VAddr constants = item + ElementColor;
    VAddr texture = TextureOf(Load<VAddr>(material + MaterialTexture));
    const VAddr mask = TextureOf(Load<VAddr>(material + MaterialMask));

    // The vertex program's colour and UV transform, and the pixel program's colour.
    std::array<float, 4> vs_color = White;
    std::array<float, 4> uv = PlainUv;
    const float* ps_color = nullptr;
    const auto uv_of = [&] {
        const auto values = Load<std::array<float, 4>>(item + ElementUv);
        if (values[2] != 0.5f || values[3] != 0.5f || values[0] != 1.0f || values[1] != 0.0f) {
            uv = values;
        }
    };
    std::array<float, 4> pixel_color{};
    switch (type) {
    case 2:
        vs_color = Load<std::array<float, 4>>(constants);
        break;
    case 3:
    case 4: {
        bool colored = false;
        if (type == 3 && Load<u32>(def + DefTextured) == 0) {
            texture = Game(DefaultTexture);
            vs_color = Load<std::array<float, 4>>(Game(UntexturedColor));
            colored = true;
        }
        uv_of();
        pixel_color = Load<std::array<float, 4>>(item + ElementPixelColor);
        ps_color = pixel_color.data();
        if (texture == 0) {
            texture = Game(DefaultTexture);
            vs_color = Load<std::array<float, 4>>(Game(MissingColor));
        } else if (!colored) {
            vs_color = Load<std::array<float, 4>>(constants);
            vs_color[3] = 1.0f;
        }
        break;
    }
    case 5:
        uv_of();
        pixel_color = Load<std::array<float, 4>>(constants);
        ps_color = pixel_color.data();
        break;
    case 1: {
        pixel_color = Load<std::array<float, 4>>(item + ElementPixelColor);
        VAddr source = Game(DefaultTexture);
        vs_color = Load<std::array<float, 4>>(Game(MovieColor));
        if (Load<s32>(item + ElementMovie) != -1 && texture != 0) {
            source = texture;
            vs_color = Load<std::array<float, 4>>(constants);
            vs_color[3] = 1.0f;
            if (pixel_color[1] == 0.0f && texture != Game(ScreenTarget)) {
                pixel_color[1] = 1.0f;
            }
        }
        texture = source;
        if (pixel_color[0] < 0.0f && pixel_color[1] == 1.0f) {
            const u16 width = Load<u16>(source + TargetWidth);
            const u16 height = Load<u16>(source + TargetHeight);
            pixel_color = {pixel_color[0], 0.0f, static_cast<float>(width),
                           static_cast<float>(height)};
            // A 1 x 1 movie, one not playing yet, is not converted.
            if (width != 1) {
                g_conversion.pending = true;
                g_conversion.full = width == FullMovieWidth && height == FullMovieHeight;
                Draw& convert = g_conversion.convert;
                convert = {};
                convert.vertex = Scene::UiVertexProgram::PassFlat;
                convert.pixel = Scene::UiPixelProgram::Movie;
                convert.topology = Scene::UiPrimitiveStrip;
                convert.count = 4;
                if (Bindable(source)) {
                    convert.textures[0] = TextureWords(source);
                }
                const std::array<float, 4> size{static_cast<float>(width),
                                                static_cast<float>(height), 0.0f, 0.0f};
                convert.ps_bytes = sizeof(size);
                convert.ps_constants = Keep(size.data(), convert.ps_bytes);
                const u32 pool = (Load<u32>(Game(FilledPool)) + 2) % 3;
                SetTarget(Load<u8>(Game(ScreenTargetFlags) + pool) != 0
                              ? Game(ScreenTarget)
                              : Load<VAddr>(Game(BackBuffer)));
            }
        }
        uv_of();
        ps_color = pixel_color.data();
        break;
    }
    default:
        break;
    }

    const Comic comic = ComicOf(item);
    const float* comic_constants = comic.on ? comic.values.data() : nullptr;
    const VAddr comic_texture = comic.texture;
    const u32 row = comic.row;
    std::array<float, 2> mask_range{};
    const float* mask_constants = nullptr;
    if (mask != 0) {
        mask_range[1] = Load<float>(item + ElementMaskBottom);
        const float top = Load<float>(item + ElementMaskTop);
        if (top != mask_range[1]) {
            mask_range[0] = 1.0f / (top - mask_range[1]);
            mask_range[1] = -(mask_range[0] * mask_range[1]);
        }
        mask_constants = mask_range.data();
    }
    const VAddr color_matrix = Load<VAddr>(material + MaterialColorMatrix);
    const VAddr bones = Load<VAddr>(item + ElementBones);
    const u32 bone_count = bones != 0 ? Load<u32>(bones) : 0;

    Draw draw{};
    const VAddr vertex_object = Load<VAddr>(material + MaterialVertex);
    draw.vertex = VertexProgram(vertex_object, bone_count != 0);
    const VAddr pixel_object = Load<VAddr>(material + MaterialPixel);
    draw.pixel = PixelProgram(pixel_object, row, color_matrix != 0);
    draw.target = Target();
    draw.viewport = Viewport(draw.target);
    s32 mode = Load<s32>(material + MaterialBlend);
    if (static_cast<u32>(mode) >= 0x10000) {
        mode >>= 16;
    }
    draw.blend = Blend(mode);
    draw.topology = Scene::UiPrimitiveList;
    ReadInputs(vertex_object + (bone_count != 0 ? SkinnedConfig : PlainConfig), matrix, draw);
    const VAddr index_set = Load<VAddr>(matrix - MatrixIndices);
    if (index_set != 0 && Load<s32>(index_set + SetKind) == IndexKind) {
        const VAddr indices = Load<VAddr>(index_set);
        draw.indices = Load<VAddr>(Load<VAddr>(indices));
        draw.index_bits = Load<s32>(indices + IndexType) == 0 ? 16 : 32;
    }
    draw.first = Load<u32>(item + ElementFirst);
    draw.count = Load<u32>(item + ElementCount);

    std::array<u8, MatrixBytes + 32> vs{};
    std::memcpy(vs.data(), reinterpret_cast<const void*>(matrix), MatrixBytes);
    std::memcpy(vs.data() + MatrixBytes, vs_color.data(), 16);
    std::memcpy(vs.data() + MatrixBytes + 16, uv.data(), 16);
    draw.vs_constants = Keep(vs.data(), static_cast<u32>(vs.size()));
    draw.vs_bytes = static_cast<u32>(vs.size());
    // The palette, copied as FUN_017b59b0 copies it when the element draws. Without one (the
    // palettes' arena ran out) it copies none and leaves the last one bound; this draw then has
    // none, and Write leaves it out.
    const VAddr palette = bone_count != 0 ? Load<VAddr>(bones + BonePalette) : 0;
    if (palette != 0) {
        draw.palette_bytes = bone_count * BoneBytes;
        draw.vs_palette = Keep(reinterpret_cast<const void*>(palette), draw.palette_bytes);
    }
    const float weight = Load<float>(item + ElementMatrixWeight);
    // The Color family's builder, FUN_017b6e00, has no colour; FUN_017b6a40 begins with it.
    const bool colored =
        Load<VAddr>(Load<VAddr>(pixel_object) + ObjectConstants) != Game(ColorlessConstants);
    draw.ps_constants =
        PixelConstants(colored, ps_color, comic_constants, mask_constants,
                       color_matrix != 0 ? reinterpret_cast<const float*>(color_matrix) : nullptr,
                       weight, draw.ps_bytes);

    // Textures: the texture in slot 0, the comic numbers' target in slot 1 and the mask in slot 2,
    // the default texture where one is missing, each where the binder binds it; the programs read
    // the texture and one of the others.
    const VAddr first = texture != 0 ? texture : Game(DefaultTexture);
    if (Bindable(first)) {
        draw.textures[0] = TextureWords(first);
    }
    const VAddr other = draw.pixel == Scene::UiPixelProgram::Masked ? mask : comic_texture;
    const VAddr second = other != 0 ? other : Game(DefaultTexture);
    if (Bindable(second)) {
        draw.textures[1] = TextureWords(second);
    }
    if (g_conversion.pending) {
        g_conversion.element = draw;
        return;
    }
    Add(draw);
}

// After a movie element's function: the conversion into the target it made, then the element
// drawn from that target.
void ConvertMovie(VAddr) {
    if (!g_conversion.pending) {
        return;
    }
    g_conversion.pending = false;
    const VAddr target = g_conversion.full ? Game(FullMovieTarget) : Load<VAddr>(Game(MovieTarget));
    Draw& convert = g_conversion.convert;
    convert.target = target;
    convert.viewport = Viewport(target);
    Add(convert);
    Draw& element = g_conversion.element;
    element.textures[0] = Bindable(target) ? TextureWords(target) : Texture{};
    Add(element);
}

constexpr std::array Kinds{
    Kind{BindItem, &ReadBind},
    Kind{ElementItem, &ReadElement, &ConvertMovie},
};

} // namespace

std::span<const Kind> ElementKinds() {
    static std::array<Kind, Kinds.size()> kinds = [] {
        auto resolved = Kinds;
        for (Kind& kind : resolved) {
            kind.vtable = Game(kind.vtable);
        }
        return resolved;
    }();
    return kinds;
}

} // namespace Gr2Port::UiItems
