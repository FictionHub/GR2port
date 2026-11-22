// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle pixel program 0x1bcb678, a blur sprite: the scene's colour behind the
// particle, faded by the depth behind it and coloured by its curve, times its alpha.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "particle_pixel.glsl"

// The particle's alpha in location 0's w, its distance in location 1's x, a vector whose length can
// pick the place on its curve in location 2's z and w, a value in thousandths that can pick it in
// location 3's z, and its curve in location 10's x, as bits.
layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 2) in vec4 attr2;
layout(location = 3) in vec4 attr3;
layout(location = 10) flat in vec4 attr4;
layout(location = 0) out vec4 color0;

void main() {
    ParticleDraw draw = Draw();
    const vec2 position = GamePosition(draw);
    const vec2 place = vec2(position.x * (1.0 / draw.constants[ScreenSize]),
                            position.y * (1.0 / draw.constants[ScreenSize + 1u]));
    const vec3 background =
        textureLod(sampler2D(background_texture, curve_sampler), place, 0.0).rgb;
    // The game's program adds the one sample ten times and takes a tenth.
    vec3 sum = vec3(0.0);
    for (uint tap = 0u; tap < 10u; tap++) {
        sum = background + sum;
    }
    const float fade = DepthFade(draw, place, attr1.x);
    vec3 color = 0.1 * sum;
    float alpha = clamp(max(0.0, attr0.w), 0.0, 1.0) * fade;
    float fade_scale = fade;

    // c_gpufxUsed.z holds the particle's curve from bit 4, 0 for none, and from bit 18 what picks
    // the place on it: 0 the alpha, 1 the vector's length, 2 the fade, 3 the value.
    const uint curve = bitfieldExtract(draw.gpufx_used[2], 4, 14);
    if (curve != 0u) {
        const uint pick = bitfieldExtract(draw.gpufx_used[2], 18, 4);
        float x = clamp(max(0.0, alpha), 0.0, 1.0);
        float alpha_scale = 1.0;
        if (pick != 0u) {
            alpha_scale = alpha;
            if (pick == 1u) {
                x = clamp(sqrt(attr2.w * attr2.w + attr2.z * attr2.z), 0.0, 1.0);
            } else if (pick == 2u) {
                x = fade;
                fade_scale = 1.0;
            } else if (pick == 3u) {
                x = clamp(0.001 * attr3.z, 0.0, 1.0);
            } else {
                x = 0.0;
            }
        }
        const vec4 curve_color = CurveColor(curve + floatBitsToUint(attr4.x), x);
        color = curve_color.rgb * color;
        alpha = alpha_scale * curve_color.a;
    }
    alpha = fade_scale * alpha;

    // The game's program discards alpha below 2^-23, before and after c_materialAttr2.w.
    if (uintBitsToFloat(0x34000000u) > alpha) {
        discard;
    }
    alpha = draw.constants[MaterialAttr2 + 3u] * alpha;
    if (uintBitsToFloat(0x34000000u) > alpha) {
        discard;
    }
    const vec3 premultiplied = color * alpha;
    const float coverage = clamp((1.0 - draw.constants[MaterialAttr]) * alpha, 0.0, 1.0);
    color0 = vec4(unpackHalf2x16(packHalf2x16(premultiplied.rg)),
                  unpackHalf2x16(packHalf2x16(vec2(premultiplied.b, coverage))));
}
