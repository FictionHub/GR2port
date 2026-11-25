// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's UI program 0x1b712f8, which passes each point of the ink strokes on to the stroke
// geometry program: the position, and the colour times the master colour in constants 0-3. The
// two outputs are the point's export ring dwords 0-3 and 4-7.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "ui_vertex.glsl"

layout(location = 0) out vec4 ring0;
layout(location = 1) out vec4 ring1;

void main() {
    UiDraw draw = Draw();
    // A blanked block has no constants; ui_stroke.geom gives it no stroke.
    if (draw.vertex_constants == 0ul) {
        return;
    }
    const uint vertex = Vertex(draw);
    ring0.xyz = Attribute(draw, 0u, vertex).xyz;
    ring1 = ConstantVec4(VertexConstants(draw), 0u) * Attribute(draw, 2u, vertex);
}
