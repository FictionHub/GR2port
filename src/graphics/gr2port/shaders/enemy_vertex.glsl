// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the enemy vertex programs read: Scene::EnemyDraw, the game's vertex constants and then the
// mesh, and the mesh's elements at the vertex as the hardware's vertex fetch gives them.

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer EnemyConstants {
    float constants[];
};
layout(buffer_reference, std430, buffer_reference_align = 8) readonly buffer EnemyMesh {
    uint64_t vertices;
    uint64_t skin;
    uint64_t palette;
    uint stride;
    uint displacement;
    uint elements[16];
    uint maps[8];
};
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer EnemyWords {
    uint words[];
};
layout(push_constant) uniform Push {
    uint64_t draws_address;
};
const uint64_t EnemyDrawBytes = 896ul;
const uint64_t EnemyMeshOffset = 768ul;

// The engine's semantics of the mesh's elements.
const uint SemanticPosition = 0u;
const uint SemanticNormal = 2u;
const uint SemanticTexcoord0 = 8u;
const uint SemanticTangent = 14u;

// The game's vertex formats.
const uint FormatRg32Float = 2u;
const uint FormatRgb32Float = 3u;
const uint FormatRgba32Float = 4u;
const uint FormatRg16Float = 6u;
const uint FormatRgba16Float = 7u;
const uint FormatRgba16Unorm = 10u;
const uint FormatRg16Sint = 12u;
const uint FormatRgba16Sint = 13u;
const uint FormatRgba8Unorm = 16u;
const uint FormatR11G11B10Float = 20u;

layout(location = 15) flat out uint draw_index;

// The draw's record. The instance names it, and the pixel program reads the same record.
uint64_t DrawAddress() {
    draw_index = uint(gl_BaseInstance);
    return draws_address + uint64_t(draw_index) * EnemyDrawBytes;
}

// The element's words: the components its format holds, then 0, 0 and 1. An element holds a byte
// offset in its low 8 bits and the game's vertex format above, 0 for none.
uvec4 ElementWords(EnemyMesh mesh, uint element) {
    const uint one = floatBitsToUint(1.0);
    const uint format = element >> 8;
    if (format == 0u) {
        return uvec4(0u, 0u, 0u, one);
    }
    EnemyWords at =
        EnemyWords(mesh.vertices + uint64_t(gl_VertexIndex) * mesh.stride + (element & 0xffu));
    if (format == FormatRg32Float) {
        return uvec4(at.words[0], at.words[1], 0u, one);
    }
    if (format == FormatRgb32Float) {
        return uvec4(at.words[0], at.words[1], at.words[2], one);
    }
    if (format == FormatRgba32Float) {
        return uvec4(at.words[0], at.words[1], at.words[2], at.words[3]);
    }
    if (format == FormatRg16Float) {
        return uvec4(floatBitsToUint(unpackHalf2x16(at.words[0])), 0u, one);
    }
    if (format == FormatRgba16Float) {
        return floatBitsToUint(vec4(unpackHalf2x16(at.words[0]), unpackHalf2x16(at.words[1])));
    }
    if (format == FormatRgba16Unorm) {
        return floatBitsToUint(vec4(unpackUnorm2x16(at.words[0]), unpackUnorm2x16(at.words[1])));
    }
    if (format == FormatRg16Sint) {
        const uint word = at.words[0];
        return uvec4(uint(int(word << 16) >> 16), uint(int(word) >> 16), 0u, 1u);
    }
    if (format == FormatRgba16Sint) {
        return uvec4(uint(int(at.words[0] << 16) >> 16), uint(int(at.words[0]) >> 16),
                     uint(int(at.words[1] << 16) >> 16), uint(int(at.words[1]) >> 16));
    }
    if (format == FormatRgba8Unorm) {
        return floatBitsToUint(unpackUnorm4x8(at.words[0]));
    }
    if (format == FormatR11G11B10Float) {
        // Each component's exponent and mantissa moved into a half float's.
        const uint word = at.words[0];
        return uvec4(floatBitsToUint(unpackHalf2x16((word << 4) & 0x7ff0u).x),
                     floatBitsToUint(unpackHalf2x16((word >> 7) & 0x7ff0u).x),
                     floatBitsToUint(unpackHalf2x16((word >> 17) & 0x7fe0u).x), one);
    }
    return uvec4(0u, 0u, 0u, one);
}

// Row `row` of the column-major 4x4 matrix at constant `first` times the point, with 1 as its
// fourth component, in the order of operations of the game's programs.
float PointRow(EnemyConstants constants, uint first, uint row, vec3 point) {
    const uint at = first + row;
    return constants.constants[at] * point.x +
           (constants.constants[at + 4u] * point.y +
            (constants.constants[at + 8u] * point.z + constants.constants[at + 12u]));
}

// Row `row` of the matrix's upper 3x3 times the direction.
float DirectionRow(EnemyConstants constants, uint first, uint row, vec3 direction) {
    const uint at = first + row;
    return constants.constants[at] * direction.x +
           (constants.constants[at + 4u] * direction.y +
            constants.constants[at + 8u] * direction.z);
}

vec3 TransformPoint(EnemyConstants constants, uint first, vec3 point) {
    return vec3(PointRow(constants, first, 0u, point), PointRow(constants, first, 1u, point),
                PointRow(constants, first, 2u, point));
}

vec3 TransformDirection(EnemyConstants constants, uint first, vec3 direction) {
    return vec3(DirectionRow(constants, first, 0u, direction),
                DirectionRow(constants, first, 1u, direction),
                DirectionRow(constants, first, 2u, direction));
}

// The clip position through the matrix at constant `first`, OpenGL depth (-w near to w far) turned
// to reverse depth (1 near to 0 far).
vec4 ClipPosition(EnemyConstants constants, uint first, vec3 point) {
    const float z = PointRow(constants, first, 2u, point);
    const float w = PointRow(constants, first, 3u, point);
    return vec4(PointRow(constants, first, 0u, point), PointRow(constants, first, 1u, point),
                0.5 * (w - z), w);
}
