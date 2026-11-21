// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's UI program 0x1b70c80: a triangle over the whole screen made from the vertex number,
// with texture coordinates 0 to 1 across the screen.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "ui_vertex.glsl"

layout(location = 0) out vec4 attr0;

void main() {
    UiDraw draw = Draw();
    const uint vertex = Vertex(draw);
    const uint column = vertex & 1u;
    const uint row = vertex >> 1;
    const vec2 corner = vec2(float(int(column * 4u - 1u)), float(int(row * 4u - 1u)));
    gl_Position = ClipPosition(vec4(corner, 0.0, 1.0));
    attr0 = vec4(float(int(column)) * 2.0, -2.0 * float(int(row)) + 1.0, 0.0, 0.0);
}
