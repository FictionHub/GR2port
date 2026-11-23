// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle pixel program 0x1beadf8, a GPU particle's distortion model: as
// model_distortion.frag, with the alpha dissolved by the material's noise and faded by the angle
// the model's normal makes with the view, the depth fade scaled by the material, the curve picked
// by the particle, and the alpha after c_materialAttr2.w held to the dissolve's threshold.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "particle_gpu_push.glsl"
#include "particle_frame.glsl"
#include "particle_gpu_material.glsl"
#include "particle_gpu_model.glsl"

// The strengths of red, green and blue and the particle's alpha; its distance in x and the scale
// of its dissolve's place in w; the map's texture coordinates in x and y, and a vector whose
// length can pick the place on its curve in z and w; a value in thousandths that can pick it in z;
// the frame's normal, and the place on the material's curves in w; the frame's tangent; the
// particle's curve, material set and material, as bits.
layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 2) in vec4 attr2;
layout(location = 3) in vec4 attr3;
layout(location = 6) in vec4 attr4;
layout(location = 7) in vec4 attr5;
layout(location = 10) flat in vec4 attr6;
layout(location = 0) out vec4 color0;

void main() {
    ParticleDraw draw = Draw();
    const vec2 position = GamePosition(draw);
    const uint material = GpuMaterial(draw, floatBitsToUint(attr6.y), floatBitsToUint(attr6.z));
    const uint curve_offset = floatBitsToUint(attr6.x);
    const vec2 inverse_size =
        vec2(1.0 / draw.constants[ScreenSize], 1.0 / draw.constants[ScreenSize + 1u]);
    const vec3 color = FrameScene(position, DistortionMap(draw, attr2.xy), attr4.xyz, attr5.xyz,
                                  attr0.xyz, inverse_size);

    const float fade_scale = FadeScale(draw, material, curve_offset, attr4.w);
    const vec2 threshold = DissolveThreshold(material, curve_offset, attr4.w);
    const float particle_alpha = ErodedAlpha(draw, material, curve_offset, attr4.w, attr0.w,
                                             attr2.xy, attr1.w, attr1.x, threshold.x);
    if (uintBitsToFloat(0x34000000u) > particle_alpha) {
        discard;
    }
    const float edge_fade = EdgeFade(material, curve_offset, attr4.w, attr4.z);
    if (uintBitsToFloat(0x34000000u) > edge_fade) {
        discard;
    }
    const float fade =
        clamp(DepthDistance(draw, position * inverse_size, attr1.x) * fade_scale, 0.0, 1.0);
    color0 = MaterialColor(draw, color, edge_fade * particle_alpha, fade, threshold, attr2.zw,
                           attr3.z, curve_offset);
}
