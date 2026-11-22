// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the skinned enemy and character glass vertex programs read beside enemy_vertex.glsl: the
// mesh's skin stream and its palette of bones, each three rows of a 3x4 matrix, blended by the
// vertex's weights.

#include "enemy_vertex.glsl"

// The skin stream at the vertex: four RGBA16 FLOAT weights, then four RGBA16 SINT bone indices.
vec4 SkinWeights(EnemyMesh mesh) {
    EnemyWords at = EnemyWords(mesh.skin + uint64_t(gl_VertexIndex) * 16ul);
    return vec4(unpackHalf2x16(at.words[0]), unpackHalf2x16(at.words[1]));
}

uvec4 SkinBones(EnemyMesh mesh) {
    EnemyWords at = EnemyWords(mesh.skin + uint64_t(gl_VertexIndex) * 16ul);
    return uvec4(uint(int(at.words[2] << 16) >> 16), uint(int(at.words[2]) >> 16),
                 uint(int(at.words[3] << 16) >> 16), uint(int(at.words[3]) >> 16));
}

// Row `row` of a bone's matrix: the palette holds 48 bytes a bone, addressed in 32 bits.
vec4 BoneRow(EnemyMesh mesh, uint bone, uint row) {
    EnemyWords palette = EnemyWords(mesh.palette);
    const uint word = (48u * bone + 16u * row) >> 2;
    return uintBitsToFloat(uvec4(palette.words[word], palette.words[word + 1u],
                                 palette.words[word + 2u], palette.words[word + 3u]));
}

// A row of the four bones' matrices blended by the weights, bone 0 first.
vec4 BlendedRow(EnemyMesh mesh, vec4 weights, uvec4 bones, uint row) {
    vec4 blended = BoneRow(mesh, bones.x, row) * weights.x;
    blended = BoneRow(mesh, bones.y, row) * weights.y + blended;
    blended = BoneRow(mesh, bones.z, row) * weights.z + blended;
    blended = BoneRow(mesh, bones.w, row) * weights.w + blended;
    return blended;
}

// A blended row times the point, with 1 as its fourth component.
float SkinnedRow(vec4 row, vec3 point) {
    return row.z * point.z + (row.y * point.y + (row.x * point.x + row.w));
}
