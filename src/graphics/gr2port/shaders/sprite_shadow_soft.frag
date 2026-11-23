// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle pixel program 0x1bfeb54, a GPU particle's sprite in the shadow pass with
// the soft steps: as sprite_shadow.frag, with the alpha dissolved by the material's noise; where
// the dissolve has a threshold, the alpha after c_materialAttr2.w is 0 below it and 1 above.
#version 460
#extension GL_GOOGLE_include_directive : require

#define PARTICLE_COMBINER_NEAREST_MAP3
#include "particle_gpu_push.glsl"
#include "particle_combiner.glsl"
#include "particle_gpu_material.glsl"

// The particle's alpha in w; its distance in x and the scale of its dissolve's place in w; map 0's
// texture coordinates in x and y, and a vector whose length can pick the place on its curve in z
// and w; map 1's coordinates, and a value in thousandths that can pick it in z; map 2's
// coordinates; map 3's coordinates; the place on the material's curves in w; the particle's curve,
// material set and material, as bits.
layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 2) in vec4 attr2;
layout(location = 3) in vec4 attr3;
layout(location = 4) in vec4 attr4;
layout(location = 5) in vec4 attr5;
layout(location = 6) in vec4 attr6;
layout(location = 10) flat in vec4 attr7;
layout(location = 0) out vec4 color0;

void main() {
    ParticleDraw draw = Draw();
    const uint set = floatBitsToUint(attr7.y);
    const uint material = GpuMaterial(draw, set, floatBitsToUint(attr7.z));
    const vec4 combined = CombinedMaps(draw, set, attr2.xy, attr3.xy, attr4.xy, attr5.xy);
    if (uintBitsToFloat(0x34000000u) > combined.a) {
        discard;
    }
    const uint curve_offset = floatBitsToUint(attr7.x);
    const vec2 threshold = DissolveThreshold(material, curve_offset, attr6.w);
    const float particle_alpha =
        Eroded(draw, material, curve_offset, attr6.w, attr0.a * combined.a, attr2.xy, attr1.w,
               attr1.x, threshold.x);
    if (uintBitsToFloat(0x34000000u) > particle_alpha) {
        discard;
    }
    const vec4 curved =
        UnfadedCurved(draw, combined.rgb, particle_alpha, attr2.zw, attr3.z, curve_offset);
    float alpha = curved.a;
    if (0.0 <= threshold.x) {
        alpha = alpha < threshold.x ? 0.0 : 1.0;
    }
    if (uintBitsToFloat(0x34000000u) > alpha) {
        discard;
    }
    color0 = Premultiplied(draw, curved.rgb, alpha);
}
