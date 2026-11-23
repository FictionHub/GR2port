// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle pixel program 0x1be0d68, a lit model in the fog passes: as model_lit.frag,
// with the colour through the fog before it is premultiplied.
#version 460
#extension GL_GOOGLE_include_directive : require

#define PARTICLE_PUSH_LIGHTS
#include "particle_gpu_push.glsl"
#include "particle_combiner.glsl"
#include "particle_lit.glsl"
#include "particle_fog.glsl"

// The particle's colour and alpha; its distance in x; map 0's texture coordinates in x and y, and
// a vector whose length can pick the place on its curve in z and w; map 1's coordinates, and a
// value in thousandths that can pick it in z; map 2's coordinates; map 3's coordinates; the view
// normal; the view tangent and its sign; the view position; the particle's curve and material,
// as bits.
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
    const Layers layers = CombinerLayers(draw, floatBitsToUint(attr9.y), attr2.xy, attr3.xy,
                                         attr4.xy, attr5.xy);
    const vec4 combined = Combined(draw, layers);
    if (uintBitsToFloat(0x34000000u) > combined.a) {
        discard;
    }
    // The view normal, or where c_gpufxUsed.x's bit 21 picks a layer (below 7), the layer from -1
    // to 1 through the tangent frame, negated.
    const vec3 normal = attr6.xyz;
    vec3 surface = normal;
    const uint normal_layer = bitfieldExtract(draw.gpufx_used[0], 21, 3);
    if (normal_layer < 7u) {
        const vec3 map = 2.0 * layers.color[normal_layer].rgb + -1.0;
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
    const vec2 place = vec2(position.x * (1.0 / draw.constants[ScreenSize]),
                            position.y * (1.0 / draw.constants[ScreenSize + 1u]));
    const vec3 lit = ModelPointLights(light, LightCellFirst(light, place), attr8.xyz, n,
                                      DirectionalDiffuse(light, -n_dot_l, NoLightShift),
                                      NoLightShift);
    const float fade = DepthFade(draw, place, attr1.x);
    const vec4 curved = CurvedColor(draw, (attr0.rgb * combined.rgb) * lit, attr0.a * combined.a,
                                    fade, attr2.zw, attr3.z, floatBitsToUint(attr9.x));
    color0 = Premultiplied(draw, Fogged(curved.rgb, place, attr8.xyz), curved.a);
}
