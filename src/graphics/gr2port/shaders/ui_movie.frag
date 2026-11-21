// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's movie program, UI program 0x1c01128: the movie frame in the draw's first texture,
// whose width is constant 0.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "ui_pixel.glsl"

layout(location = 0) in vec4 attr0;
layout(location = 0) out vec4 color0;

void main() {
    UiDraw draw = Draw();
    const float width = PixelConstants(draw).constants[0];
    color0 = Export(vec4(MovieColor(FirstTexture(draw), attr0.xy, width, ~0u), 1.0));
}
