// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's enemy core vertex program 0x1b31b24: the position through WorldViewProj in constants
// 16-31, and for the pixel program the tangent and normal turned by World in constants 0-15, the
// direction to WorldCamPos in constants 32-34 and the texture coordinates.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "enemy_vertex.glsl"

// The unit tangent and the tangent's w; the normal and the direction's x; the direction's y and z,
// and the texture coordinates.
layout(location = 0) out vec4 attr0;
layout(location = 1) out vec4 attr1;
layout(location = 2) out vec4 attr2;

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

    gl_Position = ClipPosition(constants, 16u, position);
    const vec3 turned = TransformDirection(constants, 0u, tangent.xyz);
    const float inverse_length =
        1.0 / sqrt(turned.z * turned.z + (turned.y * turned.y + turned.x * turned.x));
    const vec3 camera = vec3(constants.constants[32], constants.constants[33],
                             constants.constants[34]);
    const vec3 direction = camera - TransformPoint(constants, 0u, position);
    attr0 = vec4(inverse_length * turned, tangent.w);
    attr1 = vec4(TransformDirection(constants, 0u, normal), direction.x);
    attr2 = vec4(direction.yz, texcoord);
}
