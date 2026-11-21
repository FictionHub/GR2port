// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the UI's vertex programs read: the draw's block, the vertex through the draw's indices when
// it has them, and the vertex's attributes as the game's buffer loads give them.

#include "ui_matrix.glsl"

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer UiWords {
    uint words[];
};

layout(location = 15) flat out uint draw_index;

// The draw's block. The instance names it, and the pixel program reads the same block.
UiDraw Draw() {
    draw_index = uint(gl_BaseInstance);
    return UiDraw(draws_address + uint64_t(draw_index) * DrawBytes);
}

UiConstants VertexConstants(UiDraw draw) {
    return UiConstants(draw.vertex_constants);
}

// Four constants from the first.
vec4 ConstantVec4(UiConstants constants, uint first) {
    return vec4(constants.constants[first], constants.constants[first + 1u],
                constants.constants[first + 2u], constants.constants[first + 3u]);
}

// The vertex, or the draw's 16-bit or 32-bit index for it.
uint Vertex(UiDraw draw) {
    if (draw.indices == 0ul) {
        return uint(gl_VertexIndex);
    }
    UiWords indices = UiWords(draw.indices);
    const uint at = draw.first_index + uint(gl_VertexIndex);
    if (draw.index_bits == 32u) {
        return indices.words[at];
    }
    return indices.words[at >> 1] >> ((at & 1u) * 16u) & 0xffffu;
}

// A component as a destination select of the attribute's format picks it: 0, 1 or a loaded one.
uint Select(uint select, uvec4 loaded, uint one) {
    return select >= 4u ? loaded[select - 4u] : (select == 1u ? one : 0u);
}

// The vertex's attribute in a slot, as 32-bit words. Data formats 14, 13, 11 and 4 load four to one
// words, 10 four bytes and 12 four 16-bit numbers, which number format 0 normalizes. A component
// left out is 0, and 1 for the fourth.
uvec4 AttributeWords(UiDraw draw, uint slot, uint vertex) {
    const UiAttribute source = draw.attributes[slot];
    const uint data_format = bitfieldExtract(source.format, 15, 4);
    const uint number_format = bitfieldExtract(source.format, 12, 3);
    UiWords data = UiWords(source.address + uint64_t(vertex) * uint64_t(source.stride));
    const uint one = number_format == 7u || number_format == 0u ? floatBitsToUint(1.0) : 1u;
    uvec4 loaded = uvec4(0u, 0u, 0u, one);
    if (data_format == 14u) {
        loaded = uvec4(data.words[0], data.words[1], data.words[2], data.words[3]);
    } else if (data_format == 13u) {
        loaded.xyz = uvec3(data.words[0], data.words[1], data.words[2]);
    } else if (data_format == 11u) {
        loaded.xy = uvec2(data.words[0], data.words[1]);
    } else if (data_format == 4u) {
        loaded.x = data.words[0];
    } else if (data_format == 10u) {
        const uvec4 bytes = uvec4(data.words[0]) >> uvec4(0u, 8u, 16u, 24u) & 255u;
        loaded = number_format == 0u ? floatBitsToUint(vec4(bytes) / 255.0) : bytes;
    } else if (data_format == 12u) {
        const uvec4 shorts = uvec4(data.words[0] & 0xffffu, data.words[0] >> 16,
                                   data.words[1] & 0xffffu, data.words[1] >> 16);
        loaded = number_format == 0u ? floatBitsToUint(vec4(shorts) / 65535.0) : shorts;
    }
    const uvec4 selects = uvec4(source.format) >> uvec4(0u, 3u, 6u, 9u) & 7u;
    return uvec4(Select(selects.x, loaded, one), Select(selects.y, loaded, one),
                 Select(selects.z, loaded, one), Select(selects.w, loaded, one));
}

vec4 Attribute(UiDraw draw, uint slot, uint vertex) {
    return uintBitsToFloat(AttributeWords(draw, slot, vertex));
}
