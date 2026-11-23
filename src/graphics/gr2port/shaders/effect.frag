// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's effect pixel program 0x1baf378, an effect model: the effect material
// (effect_material.glsl) with the material's constants at the effect draw and its maps in the
// effect mesh, the colour times the output's colour scale and premultiplied by the alpha. The
// depth is 1 less the material's depth.
#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler texture_sampler;
layout(set = 1, binding = 0) uniform texture2D depth_texture;
layout(set = 1, binding = 1) uniform sampler depth_sampler;
layout(set = 1, binding = 2) uniform texture2D background_texture;
layout(set = 1, binding = 3) uniform sampler background_sampler;

layout(push_constant) uniform Push {
    uint64_t draws_address;
    uint64_t lights_address;
    uint64_t point_lights_address;
    uint64_t light_grid_address;
};

#include "particle_lit.glsl"

// The colour; the tangent, and its w; the normal, and the world position's x; the world
// position's y and z, and the view position's x and y; the view position's z, and the texture
// coordinates.
layout(location = 15) flat in uint draw_index;
layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 2) in vec4 attr2;
layout(location = 3) in vec4 attr3;
layout(location = 4) in vec4 attr4;
layout(location = 0) out vec4 color0;

// Scene::EffectDraw (784 bytes): the material's pixel constants from word 104, the effect mesh's
// colour maps from word 190.
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer EffectDrawWords {
    uint words[];
};

EffectDrawWords EffectDraw() {
    return EffectDrawWords(draws_address + uint64_t(draw_index) * 784ul);
}

float EffectConstant(uint word) {
    return uintBitsToFloat(EffectDraw().words[104u + word]);
}

uint EffectMap(uint map) {
    return EffectDraw().words[190u + map];
}

vec3 EffectBackground(vec2 place) {
    return textureLod(sampler2D(background_texture, background_sampler), place, 0.0).rgb;
}

// The game's size, the material's words 0 and 1, scaled to the depth texture's.
vec2 EffectGameSize() {
    return vec2(EffectConstant(0u), EffectConstant(1u));
}

float EffectSceneDepth(ivec2 pixel) {
    const vec2 target = vec2(textureSize(sampler2D(depth_texture, depth_sampler), 0));
    const ivec2 at = ivec2((vec2(pixel) + 0.5) * (target / EffectGameSize()));
    return 1.0 - texelFetch(sampler2D(depth_texture, depth_sampler), at, 0).x;
}

#include "effect_material.glsl"

void main() {
    const vec2 game = EffectGameSize();
    const vec2 target = vec2(textureSize(sampler2D(depth_texture, depth_sampler), 0));
    const vec2 place = vec2(gl_FragCoord.x * game.x / target.x, gl_FragCoord.y * game.y / target.y);
    const EffectPixel pixel = EffectPixelAt(place, attr1, vec2(attr2.w, attr3.x));
    EffectLayers layers = EffectStart(attr2.xyz, attr4.yz);
    EffectLayer(layers, 0u, pixel, true);
    EffectLayer(layers, 1u, pixel, false);
    EffectLayer(layers, 2u, pixel, false);
    EffectLayer(layers, 3u, pixel, false);
    const vec4 finished = EffectFinish(layers, pixel, attr0, vec3(attr3.zw, attr4.x));
    const float scale = EffectConstant(EffectOutputScale);
    const float alpha = finished.a;
    const vec3 premultiplied = vec3(alpha * (scale * finished.r), alpha * (scale * finished.g),
                                    alpha * (scale * finished.b));
    color0 = vec4(unpackHalf2x16(packHalf2x16(premultiplied.rg)),
                  unpackHalf2x16(packHalf2x16(vec2(premultiplied.b, alpha))));
    gl_FragDepth = 1.0 - layers.depth;
}
