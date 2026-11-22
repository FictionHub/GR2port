// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's skinned nevi body vertex program 0x1b33984: enemy.vert's outputs for the position
// moved by the vertex's four bones before the displacement. The normal, the tangent and the mesh's
// position in the outputs are not skinned.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "enemy_body.glsl"
#include "enemy_skin.glsl"

// The view tangent and the tangent's w; the normal and the colour's alpha; the tangent; the view
// normal and the view position's x; the view position's y and z and the world normal's x and y;
// the world normal's z and the mesh's position; texture coordinates 0 and 2; coordinates 3.
layout(location = 0) out vec4 attr0;
layout(location = 1) out vec4 attr1;
layout(location = 2) out vec4 attr2;
layout(location = 3) out vec4 attr3;
layout(location = 4) out vec4 attr4;
layout(location = 5) out vec4 attr5;
layout(location = 6) out vec4 attr6;
layout(location = 7) out vec4 attr7;

void main() {
    const uint64_t draw = DrawAddress();
    EnemyConstants constants = EnemyConstants(draw);
    EnemyWords words = EnemyWords(draw);
    EnemyMesh mesh = EnemyMesh(draw + EnemyMeshOffset);
    gl_Position = vec4(0.0);
    // A record with vertex address 0 collapses every vertex to position 0.
    if (mesh.vertices == 0ul) {
        return;
    }
    const vec4 color = uintBitsToFloat(ElementWords(mesh, mesh.elements[SemanticColor]));
    const vec4 tangent = uintBitsToFloat(ElementWords(mesh, mesh.elements[SemanticTangent]));
    // The normal is stored from 0 to 1.
    const vec3 normal =
        2.0 * uintBitsToFloat(ElementWords(mesh, mesh.elements[SemanticNormal]).xyz) + -1.0;
    const vec3 position =
        uintBitsToFloat(ElementWords(mesh, mesh.elements[SemanticPosition]).xyz);
    const vec2 texcoord3 = Texcoord(mesh, SemanticTexcoord3);
    const vec4 weights = SkinWeights(mesh);
    const uvec4 bones = SkinBones(mesh);
    const vec4 row0 = BlendedRow(mesh, weights, bones, 0u);
    const vec4 row1 = BlendedRow(mesh, weights, bones, 1u);
    const vec4 row2 = BlendedRow(mesh, weights, bones, 2u);

    vec3 skinned =
        vec3(SkinnedRow(row0, position), SkinnedRow(row1, position), SkinnedRow(row2, position));
    if ((words.words[Flags] & FlagDisplace) != 0u) {
        skinned = Displaced(constants, mesh, skinned, normal, texcoord3, color.r);
    }
    // After the displacement, as in the game's program.
    const vec2 texcoord0 = Texcoord(mesh, SemanticTexcoord0);
    const vec2 texcoord2 = Texcoord(mesh, SemanticTexcoord2);
    gl_Position = ClipPosition(constants, WorldViewProj, skinned);
    const vec3 view_position = TransformPoint(constants, WorldView, skinned);
    const vec3 view_normal = Normalized(TransformDirection(constants, WorldView, normal));
    const vec3 view_tangent = Normalized(TransformDirection(constants, WorldView, tangent.xyz));
    const vec3 world_normal = TransformDirection(constants, World, normal);
    attr0 = vec4(view_tangent, tangent.w);
    attr1 = vec4(normal, color.a);
    attr2 = tangent;
    attr3 = vec4(view_normal, view_position.x);
    attr4 = vec4(view_position.yz, world_normal.xy);
    attr5 = vec4(world_normal.z, position);
    attr6 = vec4(texcoord0, texcoord2);
    attr7 = vec4(texcoord3, 0.0, 0.0);
}
