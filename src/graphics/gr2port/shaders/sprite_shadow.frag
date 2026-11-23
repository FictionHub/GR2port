// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle pixel program 0x1bfd4ac, a GPU particle's sprite in the shadow pass: the
// combiner's colour (not the particle's, and map 3 read nearest and clamped) and its alpha times
// the particle's, coloured by its curve with no depth fade, the alpha discarded below 2^-23 only
// after c_materialAttr2.w, premultiplied.
#version 460
#extension GL_GOOGLE_include_directive : require

#define PARTICLE_COMBINER_NEAREST_MAP3
#include "particle_gpu_push.glsl"
#include "particle_combiner.glsl"

// The particle's alpha in w; map 0's texture coordinates in x and y, and a vector whose length can
// pick the place on its curve in z and w; map 1's coordinates, and a value in thousandths that can
// pick it in z; map 2's coordinates; map 3's coordinates; the particle's curve and material, as
// bits.
layout(location = 0) in vec4 attr0;
layout(location = 2) in vec4 attr1;
layout(location = 3) in vec4 attr2;
layout(location = 4) in vec4 attr3;
layout(location = 5) in vec4 attr4;
layout(location = 10) flat in vec4 attr5;
layout(location = 0) out vec4 color0;

void main() {
    ParticleDraw draw = Draw();
    const vec4 combined = CombinedMaps(draw, floatBitsToUint(attr5.y), attr1.xy, attr2.xy,
                                       attr3.xy, attr4.xy);
    if (uintBitsToFloat(0x34000000u) > combined.a) {
        discard;
    }
    const vec4 curved = UnfadedCurved(draw, combined.rgb, attr0.a * combined.a, attr1.zw, attr2.z,
                                      floatBitsToUint(attr5.x));
    if (uintBitsToFloat(0x34000000u) > curved.a) {
        discard;
    }
    color0 = Premultiplied(draw, curved.rgb, curved.a);
}
