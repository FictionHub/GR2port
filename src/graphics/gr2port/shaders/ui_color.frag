// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's UI program 0x1b73200: the vertex colour, premultiplied by its alpha.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "ui_pixel.glsl"

layout(location = 0) in vec4 attr0;
layout(location = 0) out vec4 color0;

void main() {
    const float alpha = attr0.w;
    color0 = Export(vec4(clamp(attr0.xyz * alpha, 0.0, 1.0), clamp(max(0.0, alpha), 0.0, 1.0)));
}
