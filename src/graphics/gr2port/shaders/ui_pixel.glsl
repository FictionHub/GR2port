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

UiConstants PixelConstants(UiDraw draw) {
    return UiConstants(draw.pixel_constants);
}

// The draw's first texture and sampler.
uvec2 FirstTexture(UiDraw draw) {
    return uvec2(draw.draw_textures[0], draw.draw_samplers[0]);
}

vec4 Sample(uvec2 texture_sampler, vec2 position) {
    return texture(sampler2D(textures[nonuniformEXT(texture_sampler.x)],
                             samplers[nonuniformEXT(texture_sampler.y)]),
                   position);
}

vec4 Export(vec4 color) {
    return vec4(unpackHalf2x16(packHalf2x16(color.xy)), unpackHalf2x16(packHalf2x16(color.zw)));
}
