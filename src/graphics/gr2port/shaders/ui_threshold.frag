// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's UI program 0x1b772f8: keeps the texture's colour where its luma lies above the low
// constant and at most the high one, with the luma's place in that range as alpha; black and
// clear elsewhere.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "ui_pixel.glsl"

layout(location = 1) in vec4 attr0;
layout(location = 0) out vec4 color0;

void main() {
    UiDraw draw = Draw();
    UiConstants constants = PixelConstants(draw);
    const float low = constants.constants[0];
    const float high = constants.constants[1];
    const vec3 color = Sample(FirstTexture(draw), attr0.xy).rgb;
    const float red_part = uintBitsToFloat(0x3e990afeu) * color.r;
    const float luma = clamp(uintBitsToFloat(0x3dea7371u) * color.b +
                                 (color.g * uintBitsToFloat(0x3f162c23u) + red_part),
                             0.0, 1.0);
    vec4 kept = vec4(0.0);
    if (high >= luma && low < luma) {
        const float inverse = 1.0 / (high - low);
        const float scaled = inverse * luma;
        kept = vec4(color, -low * inverse + scaled);
    }
    color0 = Export(kept);
}
