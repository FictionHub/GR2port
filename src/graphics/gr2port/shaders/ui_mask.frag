// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's comic mask writer, UI program 0x1b74928: the panel's number from its constants in
// red, the vertex alpha in green and alpha, and the blue premultiplied by it.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "ui_pixel.glsl"

layout(location = 0) in vec4 attr0;
layout(location = 0) out vec4 color0;

void main() {
    const float alpha = clamp(max(0.0, attr0.w), 0.0, 1.0);
    const float number = PixelConstants(Draw()).constants[4];
    color0 = Export(vec4(number, alpha, clamp(attr0.z * attr0.w, 0.0, 1.0), alpha));
}
