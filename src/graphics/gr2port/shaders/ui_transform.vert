// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's UI program 0x1b11508: the position through the matrix, the texture coordinates turned,
// and the colour times the master colour in constants 16-19.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "ui_transform.glsl"

layout(location = 0) out vec4 attr0;
layout(location = 1) out vec4 attr1;

void main() {
    UiDraw draw = Draw();
    // A blanked block has no constants and draws nothing.
    if (draw.vertex_constants == 0ul) {
        gl_Position = vec4(0.0);
        return;
    }
    UiConstants constants = VertexConstants(draw);
    const uint vertex = Vertex(draw);
    gl_Position = TransformPosition(constants, Attribute(draw, 0u, vertex).xyz);
    attr1 = vec4(TransformTexture(constants, Attribute(draw, 1u, vertex).xy), 0.0, 0.0);
    attr0 = ConstantVec4(constants, 16u) * Attribute(draw, 2u, vertex);
}
