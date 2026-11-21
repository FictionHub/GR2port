// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the UI's pixel programs read: the draw's Scene::UiDraw block with the address of the
// program's constants and its first texture and sampler, and the export through half floats that
// the game's programs make.

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 1, binding = 0) uniform sampler samplers[4];

layout(buffer_reference, std430, buffer_reference_align = 8) readonly buffer UiDraw {
    uint64_t indices;
    uint64_t vertex_constants;
    uint64_t vertex_palette;
    uint64_t pixel_constants;
    uint draw_textures[2];
    uint draw_samplers[2];
};
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer UiConstants {
    float constants[];
};
layout(push_constant) uniform Push {
    uint64_t draws_address;
};
layout(location = 15) flat in uint draw_index;

UiDraw Draw() {
    return UiDraw(draws_address + uint64_t(draw_index) * 160ul);
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
