// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The end the game's blur particle programs share: a tenth of the ten taps' sum, faded by the
// depth behind the particle and coloured by its curve, times its alpha, discarded below 2^-23
// before and after c_materialAttr2.w, premultiplied, with alpha times 1 - c_materialAttr.x.

#include "particle_pixel.glsl"

// c_gpufxUsed.z holds the particle's curve from bit 4, 0 for none, and from bit 18 what picks the
// place on it: 0 the alpha, 1 the vector's length, 2 the fade, 3 the value in thousandths.
vec4 BlurredColor(ParticleDraw draw, vec3 sum, float particle_alpha, float fade, vec2 vector,
                  float value, uint curve_offset) {
    vec3 color = 0.1 * sum;
    float alpha = clamp(max(0.0, particle_alpha), 0.0, 1.0) * fade;
    float fade_scale = fade;
    const uint curve = bitfieldExtract(draw.gpufx_used[2], 4, 14);
    if (curve != 0u) {
        const uint pick = bitfieldExtract(draw.gpufx_used[2], 18, 4);
        float x = clamp(max(0.0, alpha), 0.0, 1.0);
        float alpha_scale = 1.0;
        if (pick != 0u) {
            alpha_scale = alpha;
            if (pick == 1u) {
                x = clamp(sqrt(vector.y * vector.y + vector.x * vector.x), 0.0, 1.0);
            } else if (pick == 2u) {
                x = fade;
                fade_scale = 1.0;
            } else if (pick == 3u) {
                x = clamp(0.001 * value, 0.0, 1.0);
            } else {
                x = 0.0;
            }
        }
        const vec4 curve_color = CurveColor(curve + curve_offset, x);
        color = curve_color.rgb * color;
        alpha = alpha_scale * curve_color.a;
    }
    alpha = fade_scale * alpha;
    if (uintBitsToFloat(0x34000000u) > alpha) {
        discard;
    }
    alpha = draw.constants[MaterialAttr2 + 3u] * alpha;
    if (uintBitsToFloat(0x34000000u) > alpha) {
        discard;
    }
    const vec3 premultiplied = color * alpha;
    const float coverage = clamp((1.0 - draw.constants[MaterialAttr]) * alpha, 0.0, 1.0);
    return vec4(unpackHalf2x16(packHalf2x16(premultiplied.rg)),
                unpackHalf2x16(packHalf2x16(vec2(premultiplied.b, coverage))));
}
