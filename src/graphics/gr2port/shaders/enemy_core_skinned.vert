// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's skinned enemy core vertex program 0x1b31fb0: enemy_core.vert's outputs for the
// position moved by the vertex's four bones, and for the normal turned by each blended row over
// that row's length. The tangent is not skinned.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "enemy_skin.glsl"

// The unit tangent and the tangent's w; the normal and the direction's x; the direction's y and z,
// and the texture coordinates.
layout(location = 0) out vec4 attr0;
layout(location = 1) out vec4 attr1;
layout(location = 2) out vec4 attr2;

// A blended row times the normal, over the length of all four of the row's values.
float SkinnedNormal(vec4 row, vec3 normal) {
    const float dot3 = row.z * normal.z + (row.y * normal.y + row.x * normal.x);
    return (1.0 / sqrt(row.w * row.w + (row.z * row.z + (row.y * row.y + row.x * row.x)))) * dot3;
}

void main() {
    const uint64_t draw = DrawAddress();
    EnemyConstants constants = EnemyConstants(draw);
    EnemyMesh mesh = EnemyMesh(draw + EnemyMeshOffset);
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
    const vec3 skinned_normal = vec3(SkinnedNormal(row0, normal), SkinnedNormal(row1, normal),
                                     SkinnedNormal(row2, normal));

    gl_Position = ClipPosition(constants, 16u, skinned);
    const vec3 turned = TransformDirection(constants, 0u, tangent.xyz);
    const float inverse_length =
        1.0 / sqrt(turned.z * turned.z + (turned.y * turned.y + turned.x * turned.x));
    const vec3 camera = vec3(constants.constants[32], constants.constants[33],
                             constants.constants[34]);
    const vec3 direction = camera - TransformPoint(constants, 0u, skinned);
    attr0 = vec4(inverse_length * turned, tangent.w);
    attr1 = vec4(TransformDirection(constants, 0u, skinned_normal), direction.x);
    attr2 = vec4(direction.yz, texcoord);
}
