// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's UI program 0x1b77944: the sprite in ui_sprite.glsl through a colour matrix before it
// is premultiplied. The column-major 4x4 matrix in constants 4-19 times the sprite's colour and
// alpha, weighed by constant 20, plus the sprite weighed by constant 21: the game's constant
// builder FUN_017b6a40 writes the matrix's weight and 1 minus it there.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "ui_sprite.glsl"

layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 0) out vec4 color0;

// One row of the matrix times the colour, alpha's term first and red's last as the game's program
// adds them. The program rounds each product before its add, so none of them is fused.
float ColorMatrixRow(UiConstants constants, uint row, vec4 colour) {
    precise const float sum =
        constants.constants[4u + row] * colour.r +
        (constants.constants[8u + row] * colour.g +
         (constants.constants[12u + row] * colour.b + constants.constants[16u + row] * colour.a));
    return sum;
}

// Constant 20 times the matrix's product plus constant 21 times the colour, each product rounded.
vec4 ColorMatrix(UiConstants constants, vec4 colour) {
    const vec4 product =
        vec4(ColorMatrixRow(constants, 0u, colour), ColorMatrixRow(constants, 1u, colour),
             ColorMatrixRow(constants, 2u, colour), ColorMatrixRow(constants, 3u, colour));
    precise const vec4 mixed = constants.constants[21] * colour + constants.constants[20] * product;
    return mixed;
}

void main() {
    UiDraw draw = Draw();
    UiConstants constants = PixelConstants(draw);
    const vec4 sprite = Sprite(draw, constants, attr0, attr1.xy, ~0u);
    const vec4 color = ColorMatrix(constants, sprite);
    color0 = Export(Premultiply(color.rgb, color.a));
}
