// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's UI program 0x1b798e0, a comic panel's sprite: the sprite in ui_sprite.glsl where the
// mask target holds the panel's number. The second texture is that target, read at the pixel's
// place scaled by constants 4 and 5; the sprite shows where its red lies above constant 8 and
// below constant 9. With constant 10 set, its green times constant 10 plus constant 11 fades the
// sprite and drops it at 0; otherwise the sprite shows where its green reaches constant 11. The
// program clears the lowest bit of the x of each sprite sample. A dropped pixel skips the sprite
// and is discarded at the end, as in the game's program.
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
    const vec2 place = vec2(constants.constants[4] * gl_FragCoord.x,
                            constants.constants[5] * gl_FragCoord.y);
    const vec2 mask = SampleTop(SecondTexture(draw), place).xy;
    bool shown = !(constants.constants[8] >= mask.x) && !(constants.constants[9] <= mask.x);
    float fade = 1.0;
    if (shown) {
        const float fade_scale = constants.constants[10];
        const float fade_offset = constants.constants[11];
        if (fade_scale != 0.0) {
            fade = clamp(fade_scale * mask.y + fade_offset, 0.0, 1.0);
            shown = !(0.0 == fade);
        } else {
            shown = !(fade_offset > mask.y);
        }
    }
    uvec2 packed = uvec2(0u);
    if (shown) {
        const vec4 sprite = Sprite(draw, constants, attr0, attr1.xy, XBits);
        packed = ExportPack(Premultiply(sprite.rgb, sprite.a * fade));
    }
    if (!shown) {
        discard;
    }
    color0 = ExportUnpack(packed);
}
