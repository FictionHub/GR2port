// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the UI's pixel programs read: the draw's block with the address of the program's constants
// and its first texture and sampler, and the export through half floats that the game's programs
// make.

#extension GL_EXT_nonuniform_qualifier : require

#include "ui_draw.glsl"

layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 1, binding = 0) uniform sampler samplers[4];

layout(location = 15) flat in uint draw_index;

UiDraw Draw() {
    return UiDraw(draws_address + uint64_t(draw_index) * DrawBytes);
}

// The draw's pixel constants. A blanked block has none, and its pixels are dropped.
UiConstants PixelConstants(UiDraw draw) {
    if (draw.pixel_constants == 0ul) {
        discard;
    }
    return UiConstants(draw.pixel_constants);
}

// The draw's first texture and sampler.
uvec2 FirstTexture(UiDraw draw) {
    return uvec2(draw.draw_textures[0], draw.draw_samplers[0]);
}

// The draw's second texture and sampler.
uvec2 SecondTexture(UiDraw draw) {
    return uvec2(draw.draw_textures[1], draw.draw_samplers[1]);
}

vec4 Sample(uvec2 texture_sampler, vec2 position) {
    return texture(sampler2D(textures[nonuniformEXT(texture_sampler.x)],
                             samplers[nonuniformEXT(texture_sampler.y)]),
                   position);
}

// A read of the texture's top level.
vec4 SampleTop(uvec2 texture_sampler, vec2 position) {
    return textureLod(sampler2D(textures[nonuniformEXT(texture_sampler.x)],
                                samplers[nonuniformEXT(texture_sampler.y)]),
                      position, 0.0);
}

float KeepBits(float value, uint bits) {
    return uintBitsToFloat(floatBitsToUint(value) & bits);
}

// A movie frame's colour: its luma fills the texture's top two thirds and its chroma pairs Cb, Cr
// side by side in the bottom third, converted to RGB with BT.601's video-range factors. Each
// sample's x keeps only x_bits.
vec3 MovieColor(uvec2 texture_sampler, vec2 position, float width, uint x_bits) {
    const float x = position.x;
    const float column = floor(width * x);
    const float half_parity = fract(abs(0.5 * column)) * 2.0;
    const float parity = 0.0 > column ? -half_parity : half_parity;
    const float texel = 1.0 / width;
    const bool even = 0.0 == parity;
    const float chroma_y = uintBitsToFloat(0x3eaaaaaau) * position.y + uintBitsToFloat(0x3f2aaaaau);
    const float luma_y = uintBitsToFloat(0x3f2aaaaau) * position.y;
    const float cb_x = KeepBits(even ? x : x - texel, x_bits);
    const float cr_x = KeepBits(even ? x + texel : x, x_bits);
    const float cb = Sample(texture_sampler, vec2(cb_x, chroma_y)).x;
    const float luma = Sample(texture_sampler, vec2(KeepBits(x, x_bits), luma_y)).x;
    const float cr = Sample(texture_sampler, vec2(cr_x, chroma_y)).x;
    const float y = uintBitsToFloat(0x3f9507c8u) * luma + uintBitsToFloat(0xbd9507c8u);
    const float green_cb = fma(cb, uintBitsToFloat(0xbec890d6u), uintBitsToFloat(0x3e4890d6u));
    const float green_cr = fma(cr, uintBitsToFloat(0xbf501a37u), uintBitsToFloat(0x3ed01a37u));
    const float red_cr = fma(cr, uintBitsToFloat(0x3fcc432du), uintBitsToFloat(0xbf4c432du));
    const float blue_cb = fma(cb, uintBitsToFloat(0x40011687u), uintBitsToFloat(0xbf811687u));
    return vec3(y + red_cr, (y + green_cb) + green_cr, y + blue_cb);
}

vec4 Export(vec4 color) {
    return vec4(unpackHalf2x16(packHalf2x16(color.xy)), unpackHalf2x16(packHalf2x16(color.zw)));
}

// Export in two halves, for a program whose packing and unpacking lie on either side of a branch.
// The driver drops the round through half floats where the two meet, so where they lie keeps the
// game's program's rounding.
uvec2 ExportPack(vec4 color) {
    return uvec2(packHalf2x16(color.xy), packHalf2x16(color.zw));
}

vec4 ExportUnpack(uvec2 packed) {
    return vec4(unpackHalf2x16(packed.x), unpackHalf2x16(packed.y));
}

// The colour times alpha and the alpha, each held to 0..1.
vec4 Premultiply(vec3 color, float alpha) {
    return vec4(clamp(color * alpha, 0.0, 1.0), clamp(max(0.0, alpha), 0.0, 1.0));
}
