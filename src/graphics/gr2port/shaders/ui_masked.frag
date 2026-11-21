// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's UI program 0x1b78414: the sprite in ui_sprite.glsl where a mask in the second texture
// passes. With constant 6 set, the mask times constant 6 plus constant 7 fades the sprite and drops
// it at 0; otherwise the sprite shows where the mask reaches constant 7. The program clears the
// lowest bit of the x of each sample.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "ui_sprite.glsl"

layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 0) out vec4 color0;

const uint XBits = ~1u;

void main() {
    UiDraw draw = Draw();
    UiConstants constants = PixelConstants(draw);
    const float mask = Sample(SecondTexture(draw), vec2(KeepBits(attr1.z, XBits), attr1.w)).x;
    const float fade_scale = constants.constants[6];
    const float fade_offset = constants.constants[7];
    float fade;
    if (fade_scale != 0.0) {
        fade = clamp(fade_scale * mask + fade_offset, 0.0, 1.0);
        if (0.0 == fade) {
            discard;
        }
    } else {
        fade = 1.0;
        if (fade_offset > mask) {
            discard;
        }
    }
    const vec4 sprite = Sprite(draw, constants, attr0, attr1.xy, XBits);
    color0 = Export(Premultiply(sprite.rgb, fade * sprite.a));
}
