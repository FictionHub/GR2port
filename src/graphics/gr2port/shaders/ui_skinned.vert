// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's UI program 0x1b11848: the skinned position in ui_skinned.glsl from the bones in slot
// 3 and the weights in slot 4, the texture coordinates turned, and the colour times the master
// colour in constants 16-19.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "ui_skinned.glsl"

layout(location = 0) out vec4 attr0;
layout(location = 1) out vec4 attr1;

void main() {
    UiDraw draw = Draw();
    UiConstants constants = VertexConstants(draw);
    const uint vertex = Vertex(draw);
    gl_Position = SkinnedPosition(draw, constants, Attribute(draw, 0u, vertex).xyz,
                                  AttributeWords(draw, 3u, vertex), Attribute(draw, 4u, vertex));
    attr1 = vec4(TransformTexture(constants, Attribute(draw, 1u, vertex).xy), 0.0, 0.0);
    attr0 = ConstantVec4(constants, 16u) * Attribute(draw, 2u, vertex);
}
