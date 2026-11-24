// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle pixel program 0x1be2fc8, a GPU particle's lit model in the fog passes: as
// model_lit_soft.frag, with the colour through the fog, scaled by the material, before it is
// premultiplied.
#version 460
#extension GL_GOOGLE_include_directive : require

#define PARTICLE_COMBINER_EDGE
#define PARTICLE_PUSH_LIGHTS
#include "particle_gpu_push.glsl"
#include "particle_combiner.glsl"
#include "particle_gpu_material.glsl"
#include "particle_gpu_model.glsl"
#include "particle_lit.glsl"
#include "particle_gpu_lit.glsl"
#include "particle_fog.glsl"
#include "particle_gpu_fog.glsl"

// The particle's colour and alpha; its distance in x and the scale of its dissolve's place in w;
// map 0's texture coordinates in x and y, and a vector whose length can pick the place on its curve
// in z and w; map 1's coordinates, and a value in thousandths that can pick it in z; map 2's
// coordinates; map 3's coordinates; the view normal, and the place on the material's curves in w;
// the view tangent and its sign; the view position; the particle's curve, material set and
// material, as bits.
layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 2) in vec4 attr2;
layout(location = 3) in vec4 attr3;
layout(location = 4) in vec4 attr4;
layout(location = 5) in vec4 attr5;
layout(location = 6) in vec4 attr6;
layout(location = 7) in vec4 attr7;
layout(location = 8) in vec4 attr8;
layout(location = 10) flat in vec4 attr9;
layout(location = 0) out vec4 color0;

void main() {
    ParticleDraw draw = Draw();
    const vec2 position = GamePosition(draw);
    const uint set = floatBitsToUint(attr9.y);
    const uint material = GpuMaterial(draw, set, floatBitsToUint(attr9.z));
    const Layers layers = CombinerLayers(draw, set, attr2.xy, attr3.xy, attr4.xy, attr5.xy);
    const vec4 combined = Combined(draw, layers);
    if (uintBitsToFloat(0x34000000u) > combined.a) {
        discard;
    }
    const uint curve_offset = floatBitsToUint(attr9.x);
    const float fade_scale = FadeScale(draw, material, curve_offset, attr6.w);
    const vec2 threshold = DissolveThreshold(material, curve_offset, attr6.w);
    const float particle_alpha =
        Eroded(draw, material, curve_offset, attr6.w, attr0.a * combined.a, attr2.xy, attr1.w,
               attr1.x, threshold.x);
    if (uintBitsToFloat(0x34000000u) > particle_alpha) {
        discard;
    }
    const float edge_fade = EdgeFade(material, curve_offset, attr6.w, attr6.z);
    if (uintBitsToFloat(0x34000000u) > edge_fade) {
        discard;
    }
    // The view normal, or where c_gpufxUsed.x's bit 21 picks a layer (below 7), the layer from -1
    // to 1 through the tangent frame, negated.
    const vec3 normal = attr6.xyz;
    vec3 surface = normal;
    const uint normal_layer = bitfieldExtract(draw.gpufx_used[0], 21, 3);
    if (normal_layer < 7u) {
        const vec3 map = 2.0 * LayerAt(layers, normal_layer).color.rgb + -1.0;
        const vec3 tangent = attr7.xyz;
        const vec3 bitangent = vec3(tangent.y * -normal.z + normal.y * tangent.z,
                                    tangent.z * -normal.x + normal.z * tangent.x,
                                    tangent.x * -normal.y + normal.x * tangent.y) *
                               Handedness(attr7.w);
        surface = -(map.z * normal + (map.y * bitangent + tangent * map.x));
    }
    const vec3 n =
        (1.0 / sqrt(surface.z * surface.z + (surface.y * surface.y + surface.x * surface.x))) *
        surface;

    LightConstants light = LightConstants(light_address);
    const float n_dot_l = light.words[6] * n.z + (light.words[5] * n.y + light.words[4] * n.x);
    const vec2 shift = LightShift(material, curve_offset, attr6.w);
    const vec2 place = vec2(position.x * (1.0 / draw.constants[ScreenSize]),
                            position.y * (1.0 / draw.constants[ScreenSize + 1u]));
    const vec3 lit = ModelPointLights(light, LightCellFirst(light, place), attr8.xyz, n,
                                      DirectionalDiffuse(light, -n_dot_l, shift), shift);
    const float fade = clamp(DepthDistance(draw, place, attr1.x) * fade_scale, 0.0, 1.0);
    const vec4 curved = MaterialCurved(draw, (attr0.rgb * combined.rgb) * lit,
                                       edge_fade * particle_alpha, fade, threshold, attr2.zw,
                                       attr3.z, curve_offset);
    color0 = Premultiplied(
        draw, MaterialFogged(material, curve_offset, attr6.w, curved.rgb, place, attr8.xyz),
        curved.a);
}
