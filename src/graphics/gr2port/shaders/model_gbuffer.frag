// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle pixel program 0x1bec5e8, a model in the G-buffer pass: as
// sprite_plain.frag, with the particle's normal packed into the second target and fixed values in
// the third.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "particle_gpu_push.glsl"
#include "particle_combiner.glsl"

// The particle's colour and alpha; its distance in x; map 0's texture coordinates in x and y, and
// a vector whose length can pick the place on its curve in z and w; map 1's coordinates, and a
// value in thousandths that can pick it in z; map 2's coordinates; map 3's coordinates; the
// particle's normal; the particle's curve and material, as bits.
layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 2) in vec4 attr2;
layout(location = 3) in vec4 attr3;
layout(location = 4) in vec4 attr4;
layout(location = 5) in vec4 attr5;
layout(location = 6) in vec4 attr6;
layout(location = 10) flat in vec4 attr7;
layout(location = 0) out vec4 color0;
layout(location = 1) out vec4 color1;
layout(location = 2) out vec4 color2;

void main() {
    ParticleDraw draw = Draw();
    const vec2 position = GamePosition(draw);
    const vec4 combined = CombinedMaps(draw, floatBitsToUint(attr7.y), attr2.xy, attr3.xy,
                                       attr4.xy, attr5.xy);
    if (uintBitsToFloat(0x34000000u) > combined.a) {
        discard;
    }
    const vec2 place = vec2(position.x * (1.0 / draw.constants[ScreenSize]),
                            position.y * (1.0 / draw.constants[ScreenSize + 1u]));
    const float fade = DepthFade(draw, place, attr1.x);
    color0 = CombinedColor(draw, attr0.rgb * combined.rgb, attr0.a * combined.a, fade, attr2.zw,
                           attr3.z, floatBitsToUint(attr7.x));
    color1 = vec4(unpackHalf2x16(packHalf2x16(GBufferNormal(attr6.xyz))), 0.0, 0.0);
    // 1, 1, 1 and 0.1 as a half.
    color2 = vec4(1.0, 1.0, 1.0, 0.0999755859375);
}
