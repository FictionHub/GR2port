// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the nevi body's vertex programs share beside enemy_vertex.glsl: the body's vertex constants,
// the texture table, and the displacement along the normal.

#extension GL_EXT_nonuniform_qualifier : require

#include "enemy_vertex.glsl"

#include "texture_slots.glsl"
layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler texture_sampler;

// The body's vertex constants: World, WorldView, WorldViewProj, the displacement map's Texture
// matrix, DispIntensity and the flags, whose bit 14 turns the displacement on.
const uint World = 0u;
const uint WorldView = 16u;
const uint WorldViewProj = 32u;
const uint Texture = 48u;
const uint DispIntensity = 64u;
const uint Flags = 65u;
const uint FlagDisplace = 0x4000u;

const uint SemanticColor = 3u;
const uint SemanticTexcoord2 = 10u;
const uint SemanticTexcoord3 = 11u;

// 16-bit integer texture coordinates, which the game's programs scale by 1/1024.
vec2 Texcoord(EnemyMesh mesh, uint semantic) {
    return vec2(ivec2(ElementWords(mesh, mesh.elements[semantic]).xy)) * (1.0 / 1024.0);
}

// The position moved along the normal by the displacement map, read through the Texture matrix
// and taken from -1 to 1, times DispIntensity and the vertex colour's red.
vec3 Displaced(EnemyConstants constants, EnemyMesh mesh, vec3 position, vec3 normal,
               vec2 texcoord, float red) {
    const vec2 place = vec2(constants.constants[Texture] * texcoord.x +
                                (constants.constants[Texture + 4u] * texcoord.y +
                                 constants.constants[Texture + 12u]),
                            constants.constants[Texture + 1u] * texcoord.x +
                                (constants.constants[Texture + 5u] * texcoord.y +
                                 constants.constants[Texture + 13u]));
    const float height =
        textureLod(sampler2D(textures[nonuniformEXT(mesh.displacement & TextureSlotMask)],
                             texture_sampler),
                   place, 0.0)
            .x;
    const float amount = constants.constants[DispIntensity] * red * (2.0 * height + -1.0);
    return normal * amount + position;
}

// The vector over its length, in the order of operations of the game's programs.
vec3 Normalized(vec3 v) {
    return (1.0 / sqrt(v.z * v.z + (v.y * v.y + v.x * v.x))) * v;
}
