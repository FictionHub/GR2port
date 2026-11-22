// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's character glass vertex program 0x1b29c84: the position through WorldViewProj, and for
// the pixel program the tangent and normal turned by WorldView to unit length, the view position,
// the texture coordinates, and the position moved 0.1 along the view normal through each of the
// three ShadowProj matrices.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "enemy_vertex.glsl"

// The unit tangent and the tangent's w; the places in cascades 0, 1 and 2; the unit normal and the
// view position's x; the view position's y and z and the texture coordinates.
layout(location = 0) out vec4 attr0;
layout(location = 1) out vec4 attr1;
layout(location = 2) out vec4 attr2;
layout(location = 3) out vec4 attr3;
layout(location = 4) out vec4 attr4;
layout(location = 5) out vec4 attr5;

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

void main() {
    draw_index = uint(gl_BaseInstance);
    const uint64_t draw = draws_address + uint64_t(draw_index) * GlassDrawBytes;
    EnemyConstants constants = EnemyConstants(draw);
    EnemyMesh mesh = EnemyMesh(draw + GlassMeshOffset);
    gl_Position = vec4(0.0);
    // A record with vertex address 0 collapses every vertex to position 0.
    if (mesh.vertices == 0ul) {
        return;
    }
    const vec3 position =
        uintBitsToFloat(ElementWords(mesh, mesh.elements[SemanticPosition]).xyz);
    // The normal is stored from 0 to 1.
    const vec3 normal =
        2.0 * uintBitsToFloat(ElementWords(mesh, mesh.elements[SemanticNormal]).xyz) + -1.0;
    // The game's program scales 16-bit integer texture coordinates by 1/1024.
    const vec2 texcoord =
        vec2(ivec2(ElementWords(mesh, mesh.elements[SemanticTexcoord0]).xy)) * (1.0 / 1024.0);
    const vec4 tangent = uintBitsToFloat(ElementWords(mesh, mesh.elements[SemanticTangent]));

    gl_Position = ClipPosition(constants, WorldViewProj, position);
    const vec3 view_normal = TransformDirection(constants, WorldView, normal);
    const vec3 view_tangent = TransformDirection(constants, WorldView, tangent.xyz);
    const vec3 view_position = TransformPoint(constants, WorldView, position);
    const vec3 shadow_point = view_normal * 0.1 + position;
    attr0 = vec4(Unit(view_tangent), tangent.w);
    attr1 = CascadePlace(constants, 0u, shadow_point);
    attr2 = CascadePlace(constants, 1u, shadow_point);
    attr3 = CascadePlace(constants, 2u, shadow_point);
    attr4 = vec4(Unit(view_normal), view_position.x);
    attr5 = vec4(view_position.yz, texcoord);
}
