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
    vec3 color = DistortedScene(position, direction, attr0.xyz, inverse_size);
    const uint curve_offset = floatBitsToUint(attr5.x);

    // Where c_materialAttr.y is above 0 the depth fade is scaled by the material's scale, 0.3
    // where that is below 2^-23, or by its curve.
    float fade_scale = 1.0;
    if (draw.constants[MaterialAttr + 1u] > 0.0) {
        const float stored = uintBitsToFloat(MaterialWord(material, MaterialFadeScale));
        fade_scale =
            MaterialCurve(material, MaterialFadeCurve, curve_offset, attr4.w,
                          vec4(uintBitsToFloat(0x34000000u) > stored ? 0.3 : stored))
                .x;
    }
    const vec4 dissolve = MaterialCurve(material, MaterialDissolveCurve, curve_offset, attr4.w,
                                        MaterialVector(material, MaterialDissolve));
    const float threshold = uintBitsToFloat(0x34000000u) > dissolve.x ? -1.0 : 0.01 * dissolve.x;
    const vec4 erosion = MaterialCurve(material, MaterialErosionCurve, curve_offset, attr4.w,
                                       MaterialVector(material, MaterialErosion));
    const vec2 place =
        attr2.xy * ((draw.constants[ClipPlane + 2u] * attr1.w) * (1.0 / attr1.x));
    const float particle_alpha =
        Dissolved(clamp(max(0.0, attr0.w), 0.0, 1.0), place, erosion, threshold);
    if (uintBitsToFloat(0x34000000u) > particle_alpha) {
        discard;
    }

    const float fade =
        clamp(DepthDistance(draw, position * inverse_size, attr1.x) * fade_scale, 0.0, 1.0);
    float alpha = particle_alpha * fade;
    float fade_out = fade;
    // c_gpufxUsed.z holds the curve from bit 4, 0 for none, and from bit 18 what picks the place
    // on it: 0 the alpha, 1 the vector's length, 2 the fade, 3 the value in thousandths.
    const uint curve = bitfieldExtract(draw.gpufx_used[2], 4, 14);
    if (curve > 0u) {
        float x = clamp(max(0.0, alpha), 0.0, 1.0);
        float alpha_scale = 1.0;
        const uint pick = bitfieldExtract(draw.gpufx_used[2], 18, 4);
        if (pick != 0u) {
            alpha_scale = alpha;
            if (pick == 1u) {
                x = clamp(sqrt(attr2.w * attr2.w + attr2.z * attr2.z), 0.0, 1.0);
            } else if (pick == 2u) {
                x = fade;
                fade_out = 1.0;
            } else if (pick == 3u) {
                x = clamp(0.001 * attr3.z, 0.0, 1.0);
            } else {
                x = 0.0;
            }
        }
        const vec4 curve_color = CurveColor(curve + curve_offset, x);
        color = curve_color.rgb * color;
        alpha = alpha_scale * curve_color.a;
    }
    alpha = fade_out * alpha;
    if (uintBitsToFloat(0x34000000u) > alpha) {
        discard;
    }
    alpha = draw.constants[MaterialAttr2 + 3u] * alpha;
    // Above a threshold of 0 the alpha below it goes, and where the dissolve's full mark reaches
    // 1 - 2^-23 the rest is whole.
    if (0.0 < threshold) {
        alpha = alpha < threshold ? 0.0
                                  : (uintBitsToFloat(0x3f7ffffeu) <= dissolve.y ? 1.0 : alpha);
    }
    if (uintBitsToFloat(0x34000000u) > alpha) {
        discard;
    }
    const vec3 premultiplied = alpha * color;
    const float coverage = clamp((1.0 - draw.constants[MaterialAttr]) * alpha, 0.0, 1.0);
    color0 = vec4(unpackHalf2x16(packHalf2x16(premultiplied.rg)),
                  unpackHalf2x16(packHalf2x16(vec2(premultiplied.b, coverage))));
}
