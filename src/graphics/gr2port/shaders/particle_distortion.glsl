// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the game's distortion particle programs share beside particle_pixel.glsl: the distortion
// map in the game's T0, the scene's red, green and blue each read a step along the map's
// direction, and the end that colours and fades the result by the particle's curve.

#extension GL_EXT_nonuniform_qualifier : require

#include "particle_pixel.glsl"

layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler texture_sampler;

// The handles of the game's T0 to T3, at byte 352 of the ParticleDraw, after the constants.
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer ParticleTextures {
    uint handles[4];
};
const uint64_t ParticleTexturesOffset = 352ul;

// The distortion map at the place, from -1 to 1. The game's program clears the lowest bit of the
// place's x and samples with a bias of 0. Where c_gpufxUsed.z's bit 3 is set, the map holds only
// its w, in red, and its x, y and z are 1.
vec4 DistortionMap(ParticleDraw draw, vec2 place) {
    const uint handle = ParticleTextures(draws_address + uint64_t(draw_index) * ParticleDrawBytes +
                                         ParticleTexturesOffset)
                            .handles[0];
    const vec2 at = vec2(uintBitsToFloat(floatBitsToUint(place.x) & ~1u), place.y);
    vec4 map;
    if (bitfieldExtract(draw.gpufx_used[2], 3, 1) != 0u) {
        map = vec4(1.0, 1.0, 1.0,
                   texture(sampler2D(textures[nonuniformEXT(handle)], texture_sampler), at, 0.0).x);
    } else {
        map = texture(sampler2D(textures[nonuniformEXT(handle)], texture_sampler), at, 0.0);
    }
    return 2.0 * map + -1.0;
}

// The direction the map points across the picture: its x and y over the length of all four of
// its values, y turned for the texture's.
vec2 DistortionDirection(vec4 map) {
    const float inverse_length =
        1.0 / sqrt(map.w * map.w + (map.z * map.z + (map.y * map.y + map.x * map.x)));
    return vec2(inverse_length * map.x, -inverse_length * map.y);
}

// The scene's colour at the position moved a step along the direction.
vec4 SceneTap(vec2 position, vec2 direction, float step, vec2 inverse_size) {
    return textureLod(sampler2D(background_texture, curve_sampler),
                      vec2(inverse_size.x * (direction.x * step + position.x),
                           inverse_size.y * (direction.y * step + position.y)),
                      0.0);
}

// The scene's red, green and blue, each moved ten times its own strength along the direction, read
// in the game's program's order.
vec3 DistortedScene(vec2 position, vec2 direction, vec3 strengths, vec2 inverse_size) {
    const float red = SceneTap(position, direction, 10.0 * strengths.x, inverse_size).r;
    const float blue = SceneTap(position, direction, 10.0 * strengths.z, inverse_size).b;
    const float green = SceneTap(position, direction, 10.0 * strengths.y, inverse_size).g;
    return vec3(red, green, blue);
}

// The colour faded by the depth behind the particle and coloured by its curve, times its alpha,
// discarded below 2^-23 before and after c_materialAttr2.w, premultiplied, with alpha times
// 1 - c_materialAttr.x. c_gpufxUsed.z holds the particle's curve from bit 4, 0 for none, and from
// bit 18 what picks the place on it: 0 the alpha, 1 the vector's length, 2 the fade, 3 the value
// in thousandths.
vec4 DistortedColor(ParticleDraw draw, vec3 color, float particle_alpha, float fade, vec2 vector,
                    float value, uint curve_offset) {
    float alpha = clamp(max(0.0, particle_alpha), 0.0, 1.0) * fade;
    float fade_scale = fade;
    const uint curve = bitfieldExtract(draw.gpufx_used[2], 4, 14);
    if (curve != 0u) {
        const uint pick = bitfieldExtract(draw.gpufx_used[2], 18, 4);
        float x = clamp(max(0.0, alpha), 0.0, 1.0);
        float alpha_scale = 1.0;
        if (pick != 0u) {
            alpha_scale = alpha;
            if (pick == 1u) {
                x = clamp(sqrt(vector.y * vector.y + vector.x * vector.x), 0.0, 1.0);
            } else if (pick == 2u) {
                x = fade;
                fade_scale = 1.0;
            } else if (pick == 3u) {
                x = clamp(0.001 * value, 0.0, 1.0);
            } else {
                x = 0.0;
            }
        }
        const vec4 curve_color = CurveColor(curve + curve_offset, x);
        color = curve_color.rgb * color;
        alpha = alpha_scale * curve_color.a;
    }
    alpha = fade_scale * alpha;
    if (uintBitsToFloat(0x34000000u) > alpha) {
        discard;
    }
    alpha = draw.constants[MaterialAttr2 + 3u] * alpha;
    if (uintBitsToFloat(0x34000000u) > alpha) {
        discard;
    }
    const vec3 premultiplied = color * alpha;
    const float coverage = clamp((1.0 - draw.constants[MaterialAttr]) * alpha, 0.0, 1.0);
    return vec4(unpackHalf2x16(packHalf2x16(premultiplied.rg)),
                unpackHalf2x16(packHalf2x16(vec2(premultiplied.b, coverage))));
}
