// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The skin of the effect meshes' vertices from the addresses of the mesh's skin stream and palette:
// the vertex's four weights and bones, and the palette's bones, each three rows of a 3x4 matrix,
// blended by the weights. enemy_skin.glsl has the same for the enemies' meshes.

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer SkinWords {
    uint words[];
};

// The skin stream at the vertex: four RGBA16 FLOAT weights, then four RGBA16 SINT bone indices.
// Programs without vertices define SKIN_PALETTE_BONES_ONLY to leave these out.
#ifndef SKIN_PALETTE_BONES_ONLY
vec4 SkinWeights(uint64_t skin) {
    SkinWords at = SkinWords(skin + uint64_t(gl_VertexIndex) * 16ul);
    return vec4(unpackHalf2x16(at.words[0]), unpackHalf2x16(at.words[1]));
}

uvec4 SkinBones(uint64_t skin) {
    SkinWords at = SkinWords(skin + uint64_t(gl_VertexIndex) * 16ul);
    return uvec4(uint(int(at.words[2] << 16) >> 16), uint(int(at.words[2]) >> 16),
                 uint(int(at.words[3] << 16) >> 16), uint(int(at.words[3]) >> 16));
}

#endif

// Row `row` of a bone's matrix: the palette holds 48 bytes a bone, addressed in 32 bits.
vec4 BoneRow(uint64_t palette, uint bone, uint row) {
    SkinWords words = SkinWords(palette);
    const uint word = (48u * bone + 16u * row) >> 2;
    return uintBitsToFloat(uvec4(words.words[word], words.words[word + 1u],
                                 words.words[word + 2u], words.words[word + 3u]));
}

// A row of the four bones' matrices blended by the weights, bone 0 first.
vec4 BlendedRow(uint64_t palette, vec4 weights, uvec4 bones, uint row) {
    vec4 blended = BoneRow(palette, bones.x, row) * weights.x;
    blended = BoneRow(palette, bones.y, row) * weights.y + blended;
    blended = BoneRow(palette, bones.z, row) * weights.z + blended;
    blended = BoneRow(palette, bones.w, row) * weights.w + blended;
    return blended;
}

// A blended row times the point, with 1 as its fourth component.
float SkinnedRow(vec4 row, vec3 point) {
    return row.z * point.z + (row.y * point.y + (row.x * point.x + row.w));
}

// A blended row's first three values times the direction.
float SkinnedDirection(vec4 row, vec3 direction) {
    return row.z * direction.z + (row.y * direction.y + row.x * direction.x);
}
