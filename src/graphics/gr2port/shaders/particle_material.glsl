// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the game's mesh particle programs with a material read beside particle_pixel.glsl: the
// material's constants and maps, and the texture table the maps name.

#extension GL_EXT_nonuniform_qualifier : require

#include "particle_pixel.glsl"

#include "texture_slots.glsl"
layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler texture_sampler;

// The material, at byte 880 of the ParticleDraw: the constants of the game's pixel program, then
// the handles of its maps (colour, normal, mask, layered colour, layered normal).
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer ParticleMaterial {
    float pixel_constants[72];
    uint maps[5];
};
const uint64_t ParticleMaterialOffset = 880ul;
const uint ColorMap = 0u;
const uint NormalMap = 1u;

ParticleMaterial Material() {
    return ParticleMaterial(draws_address + uint64_t(draw_index) * ParticleDrawBytes +
                            ParticleMaterialOffset);
}

vec4 SampleMap(ParticleMaterial material, uint map, vec2 place) {
    return texture(
        sampler2D(textures[nonuniformEXT(material.maps[map] & TextureSlotMask)], texture_sampler),
        place);
}

// The game has flat normals where the normal map's handle is 0.
vec4 SampleNormalMap(ParticleMaterial material, vec2 place) {
    const uint handle = material.maps[NormalMap];
    return handle == 0u ? vec4(0.50196, 0.50196, 1.0, 1.0)
                        : texture(sampler2D(textures[nonuniformEXT(handle & TextureSlotMask)],
                                            texture_sampler),
                                  place);
}

// The vector over its length, in the order of operations of the game's programs.
vec3 Normalized(vec3 v) {
    return v * (1.0 / sqrt(v.z * v.z + (v.y * v.y + v.x * v.x)));
}
