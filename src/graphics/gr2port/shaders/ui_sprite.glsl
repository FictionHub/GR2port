// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The sprite of the game's UI program 0x1b77580, which its masked forms share: the first texture
// times the colour, with constant 0 times the texture's and colour's alpha as alpha. A negative
// constant 0 is the alpha itself, and with constant 1 at 1 the texture is a movie frame of width
// constant 2 instead. Each sample's x keeps only x_bits.

#include "ui_pixel.glsl"

vec4 Sprite(UiDraw draw, UiConstants constants, vec4 colour, vec2 position, uint x_bits) {
    const uvec2 first = FirstTexture(draw);
    const vec2 at = vec2(KeepBits(position.x, x_bits), position.y);
    const float alpha_scale = constants.constants[0];
    if (alpha_scale < 0.0) {
        vec3 color;
        if (constants.constants[1] != 1.0) {
            color = Sample(first, at).rgb * colour.rgb;
        } else {
            color = MovieColor(first, position, constants.constants[2], x_bits);
        }
        return vec4(color, -alpha_scale);
    }
    const vec4 texel = Sample(first, at);
    return vec4(texel.rgb * colour.rgb, alpha_scale * colour.w * texel.w);
}
