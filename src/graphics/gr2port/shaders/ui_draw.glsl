// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// Scene::UiDraw, the block of a screen draw that the UI programs read, and the push constant that
// holds the address of the draws' blocks.

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

struct UiAttribute {
    uint64_t address;
    uint stride;
    uint format;
};
layout(buffer_reference, std430, buffer_reference_align = 8) readonly buffer UiDraw {
    uint64_t indices;
    uint64_t vertex_constants;
    uint64_t vertex_palette;
    uint64_t pixel_constants;
    uint draw_textures[2];
    uint draw_samplers[2];
    UiAttribute attributes[6];
    uint index_bits;
    uint first_index;
};
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer UiConstants {
    float constants[];
};
layout(push_constant) uniform Push {
    uint64_t draws_address;
};
const uint64_t DrawBytes = 160ul;
