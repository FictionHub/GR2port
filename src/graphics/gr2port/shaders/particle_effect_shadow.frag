// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle pixel program 0x1bf3ac4, a GPU particle's effect mesh in the shadow pass:
// the effect material (effect_material.glsl) as particle_effect.frag takes it, without red for
// green and blue, with a white background and no lights; the colour held to 0 to 1 times the
// vertex colour and the output's colour scale, premultiplied, with c_materialAttr2.w times the
// alpha. The depth is 1 less the material's depth.
#version 460
#extension GL_GOOGLE_include_directive : require

#define PARTICLE_PUSH_LIGHTS
#define EFFECT_NO_RED_FLAGS
#include "particle_gpu_push.glsl"
#include "particle_material.glsl"
#include "particle_lit.glsl"

// The colour; the tangent, and its w; the normal, and the world position's x; the world
// position's y and z, and the view position's x and y; the view position's z, and the texture
// coordinates.
layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 3) in vec4 attr2;
layout(location = 4) in vec4 attr3;
layout(location = 5) in vec4 attr4;
layout(location = 0) out vec4 color0;

float EffectConstant(uint word) {
    return Material().pixel_constants[word];
}

uint EffectMap(uint map) {
    return Material().maps[map];
}

vec3 EffectBackground(vec2 place) {
    return vec3(1.0);
}

// The game's pixels are scaled to the depth texture's.
float EffectSceneDepth(ivec2 pixel) {
    ParticleDraw draw = Draw();
    const vec2 target = vec2(textureSize(sampler2D(depth_texture, depth_sampler), 0));
    const vec2 game = vec2(draw.constants[ScreenSize], draw.constants[ScreenSize + 1u]);
    const ivec2 at = ivec2((vec2(pixel) + 0.5) * (target / game));
    return 1.0 - texelFetch(sampler2D(depth_texture, depth_sampler), at, 0).x;
}

#include "effect_material.glsl"

void main() {
    ParticleDraw draw = Draw();
    const EffectPixel pixel = EffectPixelAt(GamePosition(draw), attr1, vec2(attr2.w, attr3.x));
    EffectLayers layers = EffectStart(attr2.xyz, attr4.yz);
    EffectLayer(layers, 0u, pixel, true);
    EffectLayer(layers, 1u, pixel, false);
    EffectLayer(layers, 2u, pixel, false);
    EffectLayer(layers, 3u, pixel, false);
    // The alpha as EffectFinish takes it, in another order; the soft depth takes the pixel's own
    // view depth with its sign turned.
    const uint flags = floatBitsToUint(EffectConstant(EffectFlags));
    float alpha = ((layers.alpha_scale2 * attr0.w) * layers.fresnel) *
                  clamp(max(0.0, layers.alpha), 0.0, 1.0);
    const float reference = EffectConstant(EffectAlphaRef);
    if ((flags & EffectFlagAlphaStep) != 0u) {
        alpha = reference <= alpha ? 1.0 : 0.0;
    }
    if ((flags & EffectFlagAlphaTest) != 0u && reference >= alpha) {
        discard;
    }
    if ((flags & EffectFlagSoft) != 0u) {
        const float scale = EffectConstant(EffectInverseView);
        const float offset = EffectConstant(EffectInverseView + 1u);
        const float own = 1.0 / -(scale * (2.0 * layers.depth + -1.0) + offset);
        const float scene = EffectSceneDepth(
            ivec2(int(LightUint(pixel.place.x)), int(LightUint(pixel.place.y))));
        const float behind = 1.0 / (scale * (2.0 * scene + -1.0) + offset) - own;
        const float softness = EffectConstant(EffectSoftness);
        if (softness > behind) {
            if (0.0 >= behind) {
                discard;
            }
            alpha = (behind * alpha) * (1.0 / softness);
        }
    }
    const float scale = EffectConstant(EffectOutputScale);
    const vec3 color = vec3((scale * attr0.x) * clamp(max(0.0, layers.color.x), 0.0, 1.0),
                            (scale * attr0.y) * clamp(max(0.0, layers.color.y), 0.0, 1.0),
                            (scale * attr0.z) * clamp(max(0.0, layers.color.z), 0.0, 1.0));
    color0 = vec4(unpackHalf2x16(packHalf2x16(vec2(color.r * alpha, color.g * alpha))),
                  unpackHalf2x16(packHalf2x16(
                      vec2(color.b * alpha, draw.constants[MaterialAttr2 + 3u] * alpha))));
    gl_FragDepth = 1.0 - layers.depth;
}
