// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's UI program 0x1b84c34: the second texture times constant 21 plus the blur in
// ui_blur.glsl times constant 20, less constant 22.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "ui_blur.glsl"

layout(location = 1) in vec4 attr0;
layout(location = 0) out vec4 color0;

void main() {
    UiDraw draw = Draw();
    UiConstants constants = PixelConstants(draw);
    const vec4 history = Sample(SecondTexture(draw), attr0.xy);
    const vec4 blur = Blur(draw, constants, attr0.xy);
    color0 = Export((constants.constants[21] * history + constants.constants[20] * blur) -
                    constants.constants[22]);
}
