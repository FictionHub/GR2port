// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's UI program 0x1b84544: the blur in ui_blur.glsl.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "ui_blur.glsl"

layout(location = 1) in vec4 attr0;
layout(location = 0) out vec4 color0;

void main() {
    UiDraw draw = Draw();
    color0 = Export(Blur(draw, PixelConstants(draw), attr0.xy));
}
