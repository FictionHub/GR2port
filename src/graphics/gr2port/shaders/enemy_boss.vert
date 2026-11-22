// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's nevi body vertex program of the boss variant, 0x1b33000: the body's (enemy.vert) with
// the displacement placed by texture coordinates 1, and for the pixel program also the binormal
// in view space and texture coordinates 1.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "enemy_boss.glsl"

// The view tangent and the tangent's w; the normal and the colour's alpha; the tangent; the view
// binormal and the binormal's w; the view normal and the view position's x; the view position's y
// and z and the world normal's x and y; the world normal's z and the mesh's position; texture
// coordinates 0 and 2; coordinates 3 and 1.
layout(location = 0) out vec4 attr0;
layout(location = 1) out vec4 attr1;
layout(location = 2) out vec4 attr2;
layout(location = 3) out vec4 attr3;
layout(location = 4) out vec4 attr4;
layout(location = 5) out vec4 attr5;
layout(location = 6) out vec4 attr6;
layout(location = 7) out vec4 attr7;
layout(location = 8) out vec4 attr8;

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
    // The elements in the game's program's order. Coordinates 0, 2 and 3 are converted after the
    // displacement, as there.
    const uvec4 texcoord0_words = ElementWords(mesh, mesh.elements[SemanticTexcoord0]);
    const uvec4 texcoord2_words = ElementWords(mesh, mesh.elements[SemanticTexcoord2]);
    const uvec4 texcoord3_words = ElementWords(mesh, mesh.elements[SemanticTexcoord3]);
    const vec2 texcoord1 = Texcoord(mesh, SemanticTexcoord1);
    const vec4 binormal = uintBitsToFloat(ElementWords(mesh, mesh.elements[SemanticBinormal]));

    vec3 displaced = position;
    if ((words.words[Flags] & FlagDisplace) != 0u) {
        displaced = Displaced(constants, mesh, position, normal, texcoord1, color.r);
    }
    const vec2 texcoord3 = TexcoordOf(texcoord3_words);
    const vec2 texcoord0 = TexcoordOf(texcoord0_words);
    const vec2 texcoord2 = TexcoordOf(texcoord2_words);
    gl_Position = ClipPosition(constants, WorldViewProj, displaced);
    const vec3 view_position = TransformPoint(constants, WorldView, displaced);
    const vec3 view_normal = Normalized(TransformDirection(constants, WorldView, normal));
    const vec3 view_tangent = Normalized(TransformDirection(constants, WorldView, tangent.xyz));
    const vec3 view_binormal =
        Normalized(TransformDirection(constants, WorldView, binormal.xyz));
    const vec3 world_normal = TransformDirection(constants, World, normal);
    attr0 = vec4(view_tangent, tangent.w);
    attr1 = vec4(normal, color.a);
    attr2 = tangent;
    attr3 = vec4(view_binormal, binormal.w);
    attr4 = vec4(view_normal, view_position.x);
    attr5 = vec4(view_position.yz, world_normal.xy);
    attr6 = vec4(world_normal.z, position);
    attr7 = vec4(texcoord0, texcoord2);
    attr8 = vec4(texcoord3, texcoord1);
}
