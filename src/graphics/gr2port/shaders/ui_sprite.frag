// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's UI program 0x1b77580: the sprite in ui_sprite.glsl.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "ui_sprite.glsl"

layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 0) out vec4 color0;

void main() {
    UiDraw draw = Draw();
    const vec4 sprite = Sprite(draw, PixelConstants(draw), attr0, attr1.xy, ~0u);
    color0 = Export(Premultiply(sprite.rgb, sprite.a));
}
