// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's UI program 0x1c00dcc: the vertex colour scaled by its alpha to the power 2.2, with
// 1 - (1 - alpha) to the power 2.2 as the alpha.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "ui_pixel.glsl"

layout(location = 0) in vec4 attr0;
layout(location = 0) out vec4 color0;

void main() {
    const float alpha = attr0.w;
    const float scale = exp2(2.2 * log2(abs(alpha)));
    const float rest = exp2(2.2 * log2(abs(1.0 - alpha)));
    color0 = Export(vec4(clamp(attr0.xyz * scale, 0.0, 1.0), clamp(1.0 - rest, 0.0, 1.0)));
}
