// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle pixel program 0x1bf0308, a GPU particle's effect mesh: the effect material
// (effect_material.glsl) with the material's constants and maps at the particle's draw, then the
// particle's material: the colour times the output's colour scale, the alpha faded by the angle
// its normal makes with the view and held to the dissolve's threshold, premultiplied. The depth
// is 1 less the material's depth.
#version 460
#extension GL_GOOGLE_include_directive : require

#define PARTICLE_PUSH_LIGHTS
#define EFFECT_HALF_LAMBERT
#include "particle_gpu_push.glsl"
#include "particle_material.glsl"
#include "particle_gpu_material.glsl"
#include "particle_gpu_model.glsl"
#include "particle_lit.glsl"

// The colour; the tangent, and its w; the place on the material's curves in x; the normal, and
// the world position's x; the world position's y and z, and the view position's x and y; the view
// position's z, and the texture coordinates; the particle's curve, material set and material, as
// bits.
layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 2) in vec4 attr2;
layout(location = 3) in vec4 attr3;
layout(location = 4) in vec4 attr4;
layout(location = 5) in vec4 attr5;
layout(location = 6) flat in vec4 attr6;
layout(location = 0) out vec4 color0;

float EffectConstant(uint word) {
    return Material().pixel_constants[word];
}

uint EffectMap(uint map) {
    return Material().maps[map];
}

vec3 EffectBackground(vec2 place) {
    return textureLod(sampler2D(background_texture, curve_sampler), place, 0.0).rgb;
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
    const EffectPixel pixel = EffectPixelAt(GamePosition(draw), attr1, vec2(attr3.w, attr4.x));
    EffectLayers layers = EffectStart(attr3.xyz, attr5.yz);
    EffectLayer(layers, 0u, pixel, true);
    EffectLayer(layers, 1u, pixel, false);
    EffectLayer(layers, 2u, pixel, false);
    EffectLayer(layers, 3u, pixel, false);
    const vec4 finished = EffectFinish(layers, pixel, attr0, vec3(attr4.zw, attr5.x));
    const float scale = EffectConstant(EffectOutputScale);
    const vec3 scaled = vec3(scale * finished.r, scale * finished.g, scale * finished.b);
    float alpha = finished.a;
    vec3 premultiplied = alpha * scaled;
    // A set whose first material is 0xffffffff has none: the colour is left as it is.
    const uint set = floatBitsToUint(attr6.y);
    const uint set_first = MaterialWord(draw.gpufx_used[3] + set, MaterialSetFirst);
    if (set_first != 0xffffffffu) {
        const uint material = set_first + floatBitsToUint(attr6.z);
        const uint curve_offset = floatBitsToUint(attr6.x);
        const vec2 threshold = DissolveThreshold(material, curve_offset, attr2.x);
        if (uintBitsToFloat(0x34000000u) > alpha) {
            discard;
        }
        const float edge_fade = EdgeFade(material, curve_offset, attr2.x, attr3.z);
        if (uintBitsToFloat(0x34000000u) > edge_fade) {
            discard;
        }
        alpha = edge_fade * alpha;
        // Above a threshold of 0 the alpha below it goes, and where the dissolve's full mark
        // reaches 1 - 2^-23 the rest is whole.
        if (0.0 < threshold.x) {
            alpha = alpha < threshold.x
                        ? 0.0
                        : (uintBitsToFloat(0x3f7ffffeu) <= threshold.y ? 1.0 : alpha);
        }
        premultiplied = alpha * scaled;
    }
    if (uintBitsToFloat(0x34000000u) > alpha) {
        discard;
    }
    color0 = vec4(unpackHalf2x16(packHalf2x16(premultiplied.rg)),
                  unpackHalf2x16(packHalf2x16(vec2(premultiplied.b, alpha))));
    gl_FragDepth = 1.0 - layers.depth;
}
