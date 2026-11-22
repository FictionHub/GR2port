// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's skinned character glass vertex program 0x1b2a340: character_glass.vert's outputs for
// the position moved by the vertex's four bones, and the normal and tangent turned by the blended
// rows.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "character_glass_vertex.glsl"
#include "enemy_skin.glsl"

// The unit tangent and the tangent's w; the places in cascades 0, 1 and 2; the unit normal and the
// view position's x; the view position's y and z and the texture coordinates.
layout(location = 0) out vec4 attr0;
layout(location = 1) out vec4 attr1;
layout(location = 2) out vec4 attr2;
layout(location = 3) out vec4 attr3;
layout(location = 4) out vec4 attr4;
layout(location = 5) out vec4 attr5;

// A blended row's first three values times the direction.
float SkinnedDirection(vec4 row, vec3 direction) {
    return row.z * direction.z + (row.y * direction.y + row.x * direction.x);
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
    const vec4 weights = SkinWeights(mesh);
    const uvec4 bones = SkinBones(mesh);
    const vec4 row0 = BlendedRow(mesh, weights, bones, 0u);
    const vec4 row1 = BlendedRow(mesh, weights, bones, 1u);
    const vec4 row2 = BlendedRow(mesh, weights, bones, 2u);
    const vec3 skinned =
        vec3(SkinnedRow(row0, position), SkinnedRow(row1, position), SkinnedRow(row2, position));
    const vec3 skinned_normal = vec3(SkinnedDirection(row0, normal), SkinnedDirection(row1, normal),
                                     SkinnedDirection(row2, normal));
    const vec3 skinned_tangent =
        vec3(SkinnedDirection(row0, tangent.xyz), SkinnedDirection(row1, tangent.xyz),
             SkinnedDirection(row2, tangent.xyz));

    gl_Position = ClipPosition(constants, WorldViewProj, skinned);
    const vec3 view_normal = TransformDirection(constants, WorldView, skinned_normal);
    const vec3 view_tangent = TransformDirection(constants, WorldView, skinned_tangent);
    const vec3 view_position = TransformPoint(constants, WorldView, skinned);
    const vec3 shadow_point = view_normal * 0.1 + skinned;
    attr0 = vec4(Unit(view_tangent), tangent.w);
    attr1 = CascadePlace(constants, 0u, shadow_point);
    attr2 = CascadePlace(constants, 1u, shadow_point);
    attr3 = CascadePlace(constants, 2u, shadow_point);
    attr4 = vec4(Unit(view_normal), view_position.x);
    attr5 = vec4(view_position.yz, texcoord);
}
