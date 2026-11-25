// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle program 0x1bf0138, a particle mesh's shadow: white where its dithered alpha
// keeps the pixel, at the pixel's place scaled to the game's render size.
#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "dither.glsl"

// The game's constants of a particle draw (Scene::ParticleDraw), whose 64 and 65 are the game's
// render size.
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer ParticleDraw {
    float constants[88];
};

const uint64_t ParticleDrawBytes = 1192ul;

layout(set = 1, binding = 2) uniform texture2D depth_texture;
layout(set = 1, binding = 3) uniform sampler depth_sampler;

layout(push_constant) uniform Constants {
    uint64_t draws_address;
};

layout(location = 15) flat in uint draw_index;
layout(location = 0) in vec4 attr0;

layout(location = 0) out vec4 color0;

void main() {
    ParticleDraw draw = ParticleDraw(draws_address + uint64_t(draw_index) * ParticleDrawBytes);
    const vec2 target_size = vec2(textureSize(sampler2D(depth_texture, depth_sampler), 0));
    const vec2 position = vec2(gl_FragCoord.x * draw.constants[64] / target_size.x,
                               gl_FragCoord.y * draw.constants[65] / target_size.y);
    if (DitherNoise(position, 1.0 - gl_FragCoord.z) > attr0.x) {
        discard;
    }
    color0 = vec4(1.0);
}
