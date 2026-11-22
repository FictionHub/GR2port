// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the game's particle pixel programs read: Scene::ParticleDraw, whose constants are the game's
// PfxBufferCommon, the copies of the scene's colour and depth, and the curve texture.

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// PfxBufferCommon: c_viewMat at 0, c_projMat at 16, their inverses at 32 and 48, c_screenSize
// (width, height, width, height) at 64, c_clipPlane (near, far, far - near) at 68,
// c_materialAttr at 72, c_materialAttr2 at 76, c_index at 80, then c_gpufxUsed, which holds flags.
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer ParticleDraw {
    float constants[84];
    uint gpufx_used[4];
};
const uint64_t ParticleDrawBytes = 1192ul;
const uint ScreenSize = 64u;
const uint ClipPlane = 68u;
const uint MaterialAttr = 72u;
const uint MaterialAttr2 = 76u;

layout(set = 1, binding = 0) uniform texture2D curve_texture;
layout(set = 1, binding = 1) uniform sampler curve_sampler;
layout(set = 1, binding = 2) uniform texture2D depth_texture;
layout(set = 1, binding = 3) uniform sampler depth_sampler;
layout(set = 1, binding = 5) uniform texture2D background_texture;
layout(push_constant) uniform Push {
    uint64_t draws_address;
};
layout(location = 15) flat in uint draw_index;

ParticleDraw Draw() {
    return ParticleDraw(draws_address + uint64_t(draw_index) * ParticleDrawBytes);
}

// The pixel's place in the game's render size.
vec2 GamePosition(ParticleDraw draw) {
    const vec2 target = vec2(textureSize(sampler2D(depth_texture, depth_sampler), 0));
    return vec2(gl_FragCoord.x * draw.constants[ScreenSize] / target.x,
                gl_FragCoord.y * draw.constants[ScreenSize + 1u] / target.y);
}

// How far the scene lies behind the particle, from 0 to 1: the scene's depth at the place turned
// back to a distance through the clip planes, less the particle's distance. The game binds a white
// depth texture for materials whose c_materialAttr.y is 0.
float DepthFade(ParticleDraw draw, vec2 place, float distance) {
    const float depth =
        draw.constants[MaterialAttr + 1u] == 0.0
            ? 1.0
            : 1.0 - textureLod(sampler2D(depth_texture, depth_sampler), place, 0.0).x;
    const float clip_near = draw.constants[ClipPlane];
    const float clip_far = draw.constants[ClipPlane + 1u];
    const float inverse_range = 1.0 / (clip_far - clip_near);
    const float scale = clip_near * -clip_far * inverse_range;
    return clamp(scale * (1.0 / (-clip_far * inverse_range + depth)) + -distance, 0.0, 1.0);
}

// A curve's value at x from 0 to 1. The curve texture is 2048 texels wide: a curve's low 11 bits
// pick its column, the rest its block of 64 rows.
vec4 CurveColor(uint curve, float x) {
    const float block = float((curve >> 11) << 6);
    const float along = clamp(max(0.0, x), 0.0, 1.0) * 63.0 + block;
    const float column = float(curve & 0x7ffu);
    const vec2 coordinates =
        vec2(clamp((1.0 / 2048.0) * column + 1.0 / 4096.0, 0.0, 1.0),
             clamp((1.0 / 1024.0) * along + 1.0 / 2048.0, 0.0, 1.0));
    return textureLod(sampler2D(curve_texture, curve_sampler), coordinates, 0.0);
}
