// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's UI program 0x1b11110: the position, texture coordinates and colour as the draw gives
// them.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "ui_vertex.glsl"

layout(location = 0) out vec4 attr0;
layout(location = 1) out vec4 attr1;

void main() {
    UiDraw draw = Draw();
    const uint vertex = Vertex(draw);
    gl_Position = ClipPosition(vec4(Attribute(draw, 0u, vertex).xyz, 1.0));
    attr1 = vec4(Attribute(draw, 1u, vertex).xy, 0.0, 0.0);
    attr0 = Attribute(draw, 2u, vertex);
}
