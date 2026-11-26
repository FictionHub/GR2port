// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#version 460

layout(set = 0, binding = 1) uniform texture2D neighbor_texture;
layout(set = 0, binding = 4) uniform sampler point_sampler;

layout(location = 0) out vec4 attr0;

// The game's 64 by 60 tiles.
const ivec2 Tiles = ivec2(64, 60);

// Motion blur Low: an instance per tile, a quad over the tiles whose neighbour maximum moves a
// pixel or more, where the reconstruction blurs. The other tiles keep the scene as it is, so
// their quads fall outside the target. attr0 holds the screen's texture coordinates, as
// motion_blur.vert's do.
void main() {
    const ivec2 tile = ivec2(gl_InstanceIndex % Tiles.x, gl_InstanceIndex / Tiles.x);
    const vec2 neighbor = texelFetch(sampler2D(neighbor_texture, point_sampler), tile, 0).xy;
    // The corners of two triangles: (0, 0), (1, 0), (0, 1), (0, 1), (1, 0), (1, 1).
    const uint corner = uint(gl_VertexIndex);
    const vec2 position =
        (vec2(tile) + vec2((0x32u >> corner) & 1u, (0x2cu >> corner) & 1u)) / vec2(Tiles);
    attr0 = vec4(position, 0.0, 1.0);
    // The reconstruction's own test.
    const float length2 = max(uintBitsToFloat(0x358637bdu),
                              neighbor.y * neighbor.y + neighbor.x * neighbor.x);
    gl_Position = sqrt(length2) < 1.0 ? vec4(-2.0, -2.0, 0.0, 1.0)
                                      : vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
