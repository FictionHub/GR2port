// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's movie program, UI program 0x1c01128: a frame whose luma fills the texture's top two
// thirds and whose chroma pairs Cb, Cr side by side in the bottom third, converted to RGB with
// BT.601's video-range factors.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "ui_pixel.glsl"

layout(location = 0) in vec4 attr0;
layout(location = 0) out vec4 color0;

float Factor(uint bits) {
    return uintBitsToFloat(bits);
}

void main() {
    UiDraw draw = Draw();
    const float width = PixelConstants(draw).constants[0];
    const uvec2 first = FirstTexture(draw);
    const float x = attr0.x;
    const float column = floor(width * x);
    const float half_parity = fract(abs(0.5 * column)) * 2.0;
    const float parity = 0.0 > column ? -half_parity : half_parity;
    const float texel = 1.0 / width;
    const bool even = 0.0 == parity;
    const float chroma_y = Factor(0x3eaaaaaau) * attr0.y + Factor(0x3f2aaaaau);
    const float luma_y = Factor(0x3f2aaaaau) * attr0.y;
    const float cb = Sample(first, vec2(even ? x : x - texel, chroma_y)).x;
    const float luma = Sample(first, vec2(x, luma_y)).x;
    const float cr = Sample(first, vec2(even ? x + texel : x, chroma_y)).x;
    const float y = Factor(0x3f9507c8u) * luma + Factor(0xbd9507c8u);
    const float green_cb = fma(cb, Factor(0xbec890d6u), Factor(0x3e4890d6u));
    const float green_cr = fma(cr, Factor(0xbf501a37u), Factor(0x3ed01a37u));
    const float red_cr = fma(cr, Factor(0x3fcc432du), Factor(0xbf4c432du));
    const float blue_cb = fma(cb, Factor(0x40011687u), Factor(0xbf811687u));
    color0 = Export(vec4(y + red_cr, (y + green_cb) + green_cr, y + blue_cb, 1.0));
}
