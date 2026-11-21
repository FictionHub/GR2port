// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the game's UI programs 0x1b11508 and 0x1b11ef0 share: the position through the matrix in
// ui_matrix.glsl, and the texture coordinates turned and scaled around 0.5 by constants 20 and 21,
// then moved by 22 and 23.

#include "ui_vertex.glsl"

vec2 TransformTexture(UiConstants constants, vec2 coordinates) {
    const vec2 centred = -0.5 + coordinates;
    const float scale_cos = constants.constants[20];
    const float scale_sin = constants.constants[21];
    return vec2(scale_sin * centred.y + (scale_cos * centred.x + constants.constants[22]),
                scale_cos * centred.y + (-scale_sin * centred.x + constants.constants[23]));
}
