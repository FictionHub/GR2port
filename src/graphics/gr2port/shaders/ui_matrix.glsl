// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The clip position of the UI's vertex and geometry programs: OpenGL depth turned to reverse
// depth, and the column-major 4x4 matrix in constants 0-15 with the depth at half of w.

#include "ui_draw.glsl"

// OpenGL depth, -w near to w far, to reverse depth, 1 near to 0 far.
vec4 ClipPosition(vec4 position) {
    return vec4(position.xy, 0.5 * (position.w - position.z), position.w);
}

// One row of the matrix times the position, with 1 as its fourth component.
float MatrixRow(UiConstants constants, uint row, vec3 position) {
    return constants.constants[row] * position.x +
           (constants.constants[4u + row] * position.y +
            (constants.constants[8u + row] * position.z + constants.constants[12u + row]));
}

vec4 TransformPosition(UiConstants constants, vec3 position) {
    const float w = MatrixRow(constants, 3u, position);
    return ClipPosition(vec4(MatrixRow(constants, 0u, position),
                             MatrixRow(constants, 1u, position), 0.5 * w, w));
}
