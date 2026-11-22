// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's vertex constants as column-major 4x4 matrices, and their rows times a point or a
// direction in the order of operations of the game's programs.

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer VertexConstants {
    float constants[];
};

// Row `row` of the matrix at constant `first` times the point, with 1 as its fourth component.
float PointRow(VertexConstants constants, uint first, uint row, vec3 point) {
    const uint at = first + row;
    return constants.constants[at] * point.x +
           (constants.constants[at + 4u] * point.y +
            (constants.constants[at + 8u] * point.z + constants.constants[at + 12u]));
}

// Row `row` of the matrix's upper 3x3 times the direction.
float DirectionRow(VertexConstants constants, uint first, uint row, vec3 direction) {
    const uint at = first + row;
    return constants.constants[at] * direction.x +
           (constants.constants[at + 4u] * direction.y +
            constants.constants[at + 8u] * direction.z);
}

vec3 TransformPoint(VertexConstants constants, uint first, vec3 point) {
    return vec3(PointRow(constants, first, 0u, point), PointRow(constants, first, 1u, point),
                PointRow(constants, first, 2u, point));
}

vec4 TransformPoint4(VertexConstants constants, uint first, vec3 point) {
    return vec4(PointRow(constants, first, 0u, point), PointRow(constants, first, 1u, point),
                PointRow(constants, first, 2u, point), PointRow(constants, first, 3u, point));
}

vec3 TransformDirection(VertexConstants constants, uint first, vec3 direction) {
    return vec3(DirectionRow(constants, first, 0u, direction),
                DirectionRow(constants, first, 1u, direction),
                DirectionRow(constants, first, 2u, direction));
}

// The vector over its length, in the order of operations of the game's programs.
vec3 UnitVector(vec3 v) {
    return (1.0 / sqrt(v.z * v.z + (v.y * v.y + v.x * v.x))) * v;
}
