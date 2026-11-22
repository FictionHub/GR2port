// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the character glass vertex programs share beside enemy_vertex.glsl: the draw's record, the
// vertex constants, and the places in the three shadow cascades.

#include "enemy_vertex.glsl"

// Scene::CharacterGlassDraw: 80 words of vertex constants, 28 of pixel constants, then the mesh.
const uint64_t GlassDrawBytes = 560ul;
const uint64_t GlassMeshOffset = 432ul;

// The vertex constants, Buffer0 in the program's metadata: u_m4WorldView, u_m4WorldViewProj, then
// u_m4ShadowProj0 to 2.
const uint WorldView = 0u;
const uint WorldViewProj = 16u;
const uint ShadowProj = 32u;

vec3 Unit(vec3 v) {
    return (1.0 / sqrt(v.z * v.z + (v.y * v.y + v.x * v.x))) * v;
}

vec4 CascadePlace(EnemyConstants constants, uint cascade, vec3 point) {
    const uint first = ShadowProj + 16u * cascade;
    return vec4(PointRow(constants, first, 0u, point), PointRow(constants, first, 1u, point),
                PointRow(constants, first, 2u, point), PointRow(constants, first, 3u, point));
}
