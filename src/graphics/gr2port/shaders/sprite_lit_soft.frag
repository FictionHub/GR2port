// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle pixel program 0x1bd6c74, a GPU particle's lit sprite: as sprite_lit.frag,
// with the lights' luma shifted by the material and the soft steps of sprite_plain_soft.frag.
#version 460
#extension GL_GOOGLE_include_directive : require

#define PARTICLE_PUSH_LIGHTS
#include "particle_gpu_push.glsl"
#include "particle_combiner.glsl"
#include "particle_gpu_material.glsl"
#include "particle_lit.glsl"
#include "particle_gpu_lit.glsl"

// The particle's colour and alpha; its distance in x, its angle in y and the scale of its
// dissolve's place in w; map 0's texture coordinates in x and y, and the normal's x and y where no
// layer gives it (also a vector whose length can pick the place on its curve) in z and w; map 1's
// coordinates, and a value in thousandths that can pick it in z; map 2's coordinates; map 3's
// coordinates; the place on the material's curves in w; the particle's view position; the
// particle's curve, material set and material, as bits.
layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 2) in vec4 attr2;
layout(location = 3) in vec4 attr3;
layout(location = 4) in vec4 attr4;
layout(location = 5) in vec4 attr5;
layout(location = 6) in vec4 attr6;
layout(location = 8) in vec4 attr7;
layout(location = 10) flat in vec4 attr8;
layout(location = 0) out vec4 color0;

void main() {
    ParticleDraw draw = Draw();
    const vec2 position = GamePosition(draw);
    const uint set = floatBitsToUint(attr8.y);
    const uint material = GpuMaterial(draw, set, floatBitsToUint(attr8.z));
    const Layers layers = CombinerLayers(draw, set, attr2.xy, attr3.xy, attr4.xy, attr5.xy);
    const vec4 combined = Combined(draw, layers);
    if (uintBitsToFloat(0x34000000u) > combined.a) {
        discard;
    }
    const uint curve_offset = floatBitsToUint(attr8.x);
    const float fade_scale = FadeScale(draw, material, curve_offset, attr6.w);
    const vec2 threshold = DissolveThreshold(material, curve_offset, attr6.w);
    const float particle_alpha =
        Eroded(draw, material, curve_offset, attr6.w, attr0.a * combined.a, attr2.xy, attr1.w,
               attr1.x, threshold.x);
    if (uintBitsToFloat(0x34000000u) > particle_alpha) {
        discard;
    }
    // The normal's x and y from -1 to 1 and its blue from the layer c_gpufxUsed.x's bit 21 picks
    // (7 for the vertex's z and w and no blue), turned by the angle; its z is 1 less the blue's
    // and the turned x and y's squares.
    vec3 map = vec3(attr2.zw, 0.0);
    const uint normal_layer = bitfieldExtract(draw.gpufx_used[0], 21, 3);
    if (normal_layer < 7u) {
        map = vec3(2.0 * layers.color[normal_layer].rg + -1.0, layers.color[normal_layer].b);
    }
    const float angle = attr1.y;
    const float cosine = sin(
        6.283185307 *
        fract(fma(angle, uintBitsToFloat(0x3e22f983u), uintBitsToFloat(0x3e7ffe10u))));
    const float sine = sin(6.283185307 * fract(uintBitsToFloat(0x3e22f983u) * angle));
    const float x = fma(map.x, cosine, sine * -map.y);
    const float y = fma(cosine, map.y, map.x * sine);
    const float xy = fma(y, y, x * x);
    const float z = 1.0 - fma(map.z, map.z, xy);
    const float inverse = 1.0 / sqrt(fma(z, z, xy));
    const vec3 n = vec3(-x * inverse, inverse * y, inverse * z);

    LightConstants light = LightConstants(light_address);
    const float n_dot_l = light.words[6] * n.z + (light.words[5] * n.y + light.words[4] * n.x);
    const vec2 shift = LightShift(material, curve_offset, attr6.w);
    const vec2 place = vec2(position.x * (1.0 / draw.constants[ScreenSize]),
                            position.y * (1.0 / draw.constants[ScreenSize + 1u]));
    // c_projMat's first three rows turn the point lights' directions into the sprite's frame.
    const vec3 lit = SpritePointLights(
        light, LightCellFirst(light, place), attr7.xyz, n,
        vec3(draw.constants[16], draw.constants[17], draw.constants[18]),
        vec3(draw.constants[20], draw.constants[21], draw.constants[22]),
        vec3(draw.constants[24], draw.constants[25], draw.constants[26]),
        DirectionalDiffuse(light, n_dot_l, shift), shift);
    const float fade = clamp(DepthDistance(draw, place, attr1.x) * fade_scale, 0.0, 1.0);
    color0 = MaterialColor(draw, (attr0.rgb * combined.rgb) * lit, particle_alpha, fade,
                           threshold, attr2.zw, attr3.z, curve_offset);
}
