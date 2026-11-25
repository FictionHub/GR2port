// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's metaball pixel program 0x1ba30f0, dithered by the draw's alpha: the draw's albedo and
// surface values and the packed normal. The fourth target takes a mask of the metaballs' pixels
// where the game's program writes velocity.
#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "dither.glsl"

// The pixel constants of a metaball draw (Scene::MetaballDraw): the albedo, then roughness,
// metallic, translucency and emissive, then the alpha.
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer MetaballPixel {
    vec4 albedo;
    vec4 surface;
    float alpha;
};

const uint64_t MetaballDrawBytes = 512ul;
const uint64_t MetaballPixelOffset = 384ul;

layout(push_constant) uniform Constants {
    uint64_t draws_address;
};

layout(location = 15) flat in uint draw_index;
layout(location = 0) in vec4 attr0;

layout(location = 0) out vec4 color0;
layout(location = 1) out vec4 color1;
layout(location = 2) out vec4 color2;
layout(location = 3) out vec4 color3;

// The normal in two values: its x and y, scaled to the length its z gives them.
vec2 PackNormal(vec3 vector) {
    float length2 = vector.x * vector.x;
    length2 = vector.y * vector.y + length2;
    length2 = vector.z * vector.z + length2;
    const float inverse = 1.0 / sqrt(length2);
    const float x = vector.x * inverse;
    const float y = vector.y * inverse;
    const float z = -vector.z * inverse;
    float xy2 = x * x;
    xy2 = y * y + xy2;
    xy2 = max(uintBitsToFloat(0x358637bdu), xy2);
    const float scale = 1.0 / sqrt(xy2) * sqrt(abs(0.5 * z + 0.5));
    return vec2(scale * x, scale * y);
}

void main() {
    const uint64_t draw = draws_address + uint64_t(draw_index) * MetaballDrawBytes;
    MetaballPixel constants = MetaballPixel(draw + MetaballPixelOffset);
    if (constants.alpha < DitherNoise(gl_FragCoord.xy, 1.0 - gl_FragCoord.z)) {
        discard;
    }
    color0 = vec4(constants.albedo.xyz, 0.0);
    color1 = vec4(PackNormal(attr0.xyz), 0.0, 0.0);
    color2 = constants.surface;
    color3 = vec4(1.0);
}
