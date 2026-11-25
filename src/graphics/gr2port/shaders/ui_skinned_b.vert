// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's UI program 0x1b12270: as 0x1b11848, with a second pair of texture coordinates passed
// on as the draw gives them, the colour in slot 3, the bones in slot 4 and the weights in slot 5.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "ui_skinned.glsl"

layout(location = 0) out vec4 attr0;
layout(location = 1) out vec4 attr1;

void main() {
    UiDraw draw = Draw();
    // A blanked block has no constants and draws nothing.
    if (draw.vertex_constants == 0ul || draw.vertex_palette == 0ul) {
        gl_Position = vec4(0.0);
        return;
    }
    UiConstants constants = VertexConstants(draw);
    const uint vertex = Vertex(draw);
    gl_Position = SkinnedPosition(draw, constants, Attribute(draw, 0u, vertex).xyz,
                                  AttributeWords(draw, 4u, vertex), Attribute(draw, 5u, vertex));
    attr1 = vec4(TransformTexture(constants, Attribute(draw, 1u, vertex).xy),
                 Attribute(draw, 2u, vertex).xy);
    attr0 = ConstantVec4(constants, 16u) * Attribute(draw, 3u, vertex);
}
