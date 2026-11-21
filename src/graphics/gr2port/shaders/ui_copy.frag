// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's UI program 0x1c00f5c: the draw's first texture, held to 0..1.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "ui_pixel.glsl"

layout(location = 1) in vec4 attr0;
layout(location = 0) out vec4 color0;

void main() {
    const vec4 color = Sample(FirstTexture(Draw()), attr0.xy);
    color0 = Export(clamp(max(vec4(0.0), color), 0.0, 1.0));
}
