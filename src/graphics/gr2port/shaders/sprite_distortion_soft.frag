// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle pixel program 0x1bcfe7c, a GPU particle's distortion sprite: as
// sprite_distortion.frag, with the alpha dissolved by the material's noise, the depth fade scaled
// by the material, the curve picked by the particle, and the alpha after c_materialAttr2.w held to
// the dissolve's threshold.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "particle_gpu_push.glsl"
#include "particle_distortion.glsl"
#include "particle_gpu_material.glsl"

// The strengths of red, green and blue and the particle's alpha; its distance in x and the scale
// of its dissolve's place in w; the map's texture coordinates in x and y, and a vector whose
// length can pick the place on its curve in z and w; a value in thousandths that can pick it in z;
// the place on the material's curves in w; the particle's curve, material set and material, as
// bits.
layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 2) in vec4 attr2;
layout(location = 3) in vec4 attr3;
layout(location = 6) in vec4 attr4;
layout(location = 10) flat in vec4 attr5;
layout(location = 0) out vec4 color0;

void main() {
    ParticleDraw draw = Draw();
    const vec2 position = GamePosition(draw);
    const uint material = GpuMaterial(draw, floatBitsToUint(attr5.y), floatBitsToUint(attr5.z));
    const vec2 direction = DistortionDirection(DistortionMap(draw, attr2.xy));
    const vec2 inverse_size =
        vec2(1.0 / draw.constants[ScreenSize], 1.0 / draw.constants[ScreenSize + 1u]);
    const vec3 color = DistortedScene(position, direction, attr0.xyz, inverse_size);
    const uint curve_offset = floatBitsToUint(attr5.x);
    const float fade_scale = FadeScale(draw, material, curve_offset, attr4.w);
    const vec2 threshold = DissolveThreshold(material, curve_offset, attr4.w);
    const float particle_alpha = ErodedAlpha(draw, material, curve_offset, attr4.w, attr0.w,
                                             attr2.xy, attr1.w, attr1.x, threshold.x);
    if (uintBitsToFloat(0x34000000u) > particle_alpha) {
        discard;
    }
    const float fade =
        clamp(DepthDistance(draw, position * inverse_size, attr1.x) * fade_scale, 0.0, 1.0);
    color0 = MaterialColor(draw, color, particle_alpha, fade, threshold, attr2.zw, attr3.z,
                           curve_offset);
}
