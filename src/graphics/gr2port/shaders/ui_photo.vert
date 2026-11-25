// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's UI program 0x1b10cd0, the photo's rectangle: the position and two pairs of texture
// coordinates as the draw gives them, and the colour times the master colour in constants 0-3.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "ui_vertex.glsl"

layout(location = 0) out vec4 attr0;
layout(location = 1) out vec4 attr1;

void main() {
    UiDraw draw = Draw();
    // A blanked block has no constants and draws nothing.
    if (draw.vertex_constants == 0ul) {
        gl_Position = vec4(0.0);
        return;
    }
    const uint vertex = Vertex(draw);
    gl_Position = ClipPosition(vec4(Attribute(draw, 0u, vertex).xyz, 1.0));
    attr1 = vec4(Attribute(draw, 1u, vertex).xy, Attribute(draw, 2u, vertex).xy);
    attr0 = ConstantVec4(VertexConstants(draw), 0u) * Attribute(draw, 3u, vertex);
}
