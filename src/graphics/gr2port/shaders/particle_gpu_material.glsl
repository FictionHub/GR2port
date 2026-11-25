// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the game's GPU particle pixel programs read beside particle_pixel.glsl: the particle's
// material (560 bytes in the material buffer), its curves, the dissolve of its alpha by a noise of
// its texture coordinates and a 4x4 ordered dither, and the fade by the depth behind it. Words
// whose names the game does not give are named by what these programs do with them.

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer MaterialWords {
    uint words[];
};

// Four words of a material as one load.
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer MaterialRow {
    uvec4 row;
};

// A material (140 words): the dissolve's threshold and its full mark, its four erosion values,
// the depth fade's scale, the blur's count of taps and its step, the curves that replace those
// four where they are above 0, and the first material of the particle's set.
const uint MaterialWordsEach = 140u;
const uint MaterialDissolve = 0u;
const uint MaterialErosion = 4u;
const uint MaterialFadeScale = 8u;
const uint MaterialBlur = 20u;
const uint MaterialDissolveCurve = 120u;
const uint MaterialErosionCurve = 121u;
const uint MaterialFadeCurve = 122u;
const uint MaterialBlurCurve = 125u;
const uint MaterialSetFirst = 132u;

// The particle's material: the set at c_gpufxUsed.w plus the set the particle names, then the
// material within that set.
uint GpuMaterial(ParticleDraw draw, uint set, uint within) {
    // A set past the materials reads 0, as the game's buffer load past them gives.
    const uint first = draw.gpufx_used[3] + set;
    const uint set_first =
        MaterialWords(material_address + min(first, MaterialCount - 1u) * 560u).words[132];
    return (first < MaterialCount ? set_first : 0u) + within;
}

// The material's offset is 32 bits, so the table's base stays one value for every lane.
uint MaterialWord(uint material, uint word) {
    const uint value =
        MaterialWords(material_address + min(material, MaterialCount - 1u) * 560u).words[word];
    return material < MaterialCount ? value : 0u;
}

vec4 MaterialVector(uint material, uint word) {
    const uvec4 value =
        MaterialRow(material_address + min(material, MaterialCount - 1u) * 560u + 4u * word).row;
    return uintBitsToFloat(material < MaterialCount ? value : uvec4(0u));
}

// A material's curve at x, offset by the particle's curve, where the curve is above 0; else the
// default.
vec4 MaterialCurve(uint material, uint curve_word, uint curve_offset, float x, vec4 fallback) {
    const uint curve = MaterialWord(material, curve_word);
    return 0u < curve ? CurveColor(curve + curve_offset, x) : fallback;
}

// A noise of the place from 0 to 1, as the game's programs hash it.
float PlaceNoise(vec2 place) {
    float seed = uintBitsToFloat(0x41b92023u) * place.x;
    seed = place.y * uintBitsToFloat(0x402a91b9u) + seed;
    seed = 256.0 * seed + uintBitsToFloat(0x33d6bf95u);
    const float noise = fract(abs(uintBitsToFloat(0x4ceb79a3u) * (1.0 / seed)));
    return fract(noise * abs(seed));
}

// Eight octaves of the noise at the place, a quarter of it the finest, each further octave at half
// the scale with twice the weight; the coarsest, 1/512 of it, comes second with a weight of 1.
// `swap` reads the place's y as its x.
float DissolveNoise(vec2 place, bool swap) {
    const vec2 at = swap ? place.yx : place;
    float sum = PlaceNoise(0.25 * at) * (1.0 / 128.0) + PlaceNoise((1.0 / 512.0) * at);
    float scale = 0.125;
    float weight = 1.0 / 64.0;
    for (int octave = 0; octave < 6; octave++) {
        sum = PlaceNoise(scale * at) * weight + sum;
        scale = 0.5 * scale;
        weight = 2.0 * weight;
    }
    return sum;
}

// The game's 4x4 ordered dither, 0 to 15.
const uint DitherTable[16] = uint[16](0x0u, 0x41000000u, 0x40000000u, 0x41200000u, 0x41400000u,
                                      0x40800000u, 0x41600000u, 0x40c00000u, 0x40400000u,
                                      0x41300000u, 0x3f800000u, 0x41100000u, 0x41700000u,
                                      0x40e00000u, 0x41500000u, 0x40a00000u);

// The game's conversion to an integer: NaN gives 0, other values are clamped to the int range.
uint GameIntBits(float value) {
    return isnan(value) ? 0u : uint(int(clamp(value, -2147483648.0, 2147483520.0)));
}

// The game's conversion to an unsigned integer: NaN gives 0, other values are clamped to the
// uint range.
uint GameUint(float value) {
    return isnan(value) ? 0u : uint(clamp(value, 0.0, 4294967040.0));
}

// x modulo 4 with x's sign, as the game's fmod computes it.
float Modulo4(float x) {
    const float m = fract(abs(0.25 * x)) * 4.0;
    return 0.0 > x ? -m : m;
}

// The particle's alpha through its dissolve: with the erosion's x and z above 2^-23, the alpha
// stays where the smaller of the two noises, times the alpha, raised to erosion w and times its z,
// reaches the threshold, and where its alpha raised to erosion y and times its x reaches the
// dither at the cell the noises pick; elsewhere it is 0.
float Dissolved(float alpha, vec2 place, vec4 erosion, float threshold) {
    if (min(erosion.x, erosion.z) < uintBitsToFloat(0x34000000u)) {
        return alpha;
    }
    const float noise_x = DissolveNoise(place, false);
    const float noise_y = DissolveNoise(place, true);
    const float dissolve = min(exp2(log2(abs(alpha * noise_x)) * erosion.w) * erosion.z,
                               exp2(log2(abs(alpha * noise_y)) * erosion.w) * erosion.z);
    if (dissolve < threshold) {
        return 0.0;
    }
    const float row = place.y * noise_y + -fract(place.y * noise_y);
    const float column = place.x * noise_x + -fract(place.x * noise_x);
    const uint cell = (GameIntBits(Modulo4(row)) << 2) + GameIntBits(Modulo4(column));
    // A negative row or column puts the cell past the table, where the dither is 0.
    const float dither = cell < 16u ? (1.0 / 16.0) * uintBitsToFloat(DitherTable[cell]) : 0.0;
    return exp2(log2(abs(alpha)) * erosion.y) * erosion.x < dither ? 0.0 : alpha;
}

// How far the scene lies behind the particle, before the fade's clamp: the scene's depth at the
// place turned back to a distance through the clip planes, less the particle's distance.
float DepthDistance(ParticleDraw draw, vec2 place, float distance) {
    const float depth =
        draw.constants[MaterialAttr + 1u] == 0.0
            ? 1.0
            : 1.0 - textureLod(sampler2D(depth_texture, depth_sampler), place, 0.0).x;
    const float clip_near = draw.constants[ClipPlane];
    const float clip_far = draw.constants[ClipPlane + 1u];
    const float inverse_range = 1.0 / (clip_far - clip_near);
    const float scale = clip_near * -clip_far * inverse_range;
    return scale * (1.0 / (-clip_far * inverse_range + depth)) + -distance;
}

// Where c_materialAttr.y is above 0, the scale of the depth fade: the material's scale, 0.3 where
// that is below 2^-23, or its curve at x; elsewhere 1.
float FadeScale(ParticleDraw draw, uint material, uint curve_offset, float x) {
    float scale = 1.0;
    if (draw.constants[MaterialAttr + 1u] > 0.0) {
        const float stored = uintBitsToFloat(MaterialWord(material, MaterialFadeScale));
        scale = MaterialCurve(material, MaterialFadeCurve, curve_offset, x,
                              vec4(uintBitsToFloat(0x34000000u) > stored ? 0.3 : stored))
                    .x;
    }
    return scale;
}

// The material's dissolve at x: its threshold, -1 where its value is below 2^-23 and else a
// hundredth of it, and its full mark.
vec2 DissolveThreshold(uint material, uint curve_offset, float x) {
    const vec4 dissolve = MaterialCurve(material, MaterialDissolveCurve, curve_offset, x,
                                        MaterialVector(material, MaterialDissolve));
    return vec2(uintBitsToFloat(0x34000000u) > dissolve.x ? -1.0 : 0.01 * dissolve.x, dissolve.y);
}

// The particle's alpha dissolved by the material's erosion at x, at its texture coordinates times
// c_clipPlane.z times the scale over its distance.
float ErodedAlpha(ParticleDraw draw, uint material, uint curve_offset, float x, float alpha,
                  vec2 coordinates, float scale, float distance, float threshold) {
    const vec4 erosion = MaterialCurve(material, MaterialErosionCurve, curve_offset, x,
                                       MaterialVector(material, MaterialErosion));
    const vec2 place = coordinates * ((draw.constants[ClipPlane + 2u] * scale) * (1.0 / distance));
    return Dissolved(clamp(max(0.0, alpha), 0.0, 1.0), place, erosion, threshold);
}

// ErodedAlpha without the clamp: the combiner's programs dissolve their alpha as it is.
float Eroded(ParticleDraw draw, uint material, uint curve_offset, float x, float alpha,
             vec2 coordinates, float scale, float distance, float threshold) {
    const vec4 erosion = MaterialCurve(material, MaterialErosionCurve, curve_offset, x,
                                       MaterialVector(material, MaterialErosion));
    const vec2 place = coordinates * ((draw.constants[ClipPlane + 2u] * scale) * (1.0 / distance));
    return Dissolved(alpha, place, erosion, threshold);
}

// The colour faded by the depth behind the particle and coloured by its curve, times its alpha,
// discarded below 2^-23 before and after c_materialAttr2.w and held to the dissolve's threshold,
// premultiplied, with alpha times 1 - c_materialAttr.x. c_gpufxUsed.z holds the curve from bit 4,
// 0 for none, and from bit 18 what picks the place on it: 0 the alpha, 1 the vector's length, 2
// the fade, 3 the value in thousandths.
vec4 MaterialColor(ParticleDraw draw, vec3 color, float particle_alpha, float fade,
                   vec2 threshold, vec2 vector, float value, uint curve_offset) {
    float alpha = particle_alpha * fade;
    float fade_out = fade;
    const uint curve = bitfieldExtract(draw.gpufx_used[2], 4, 14);
    if (curve > 0u) {
        float x = clamp(max(0.0, alpha), 0.0, 1.0);
        float alpha_scale = 1.0;
        const uint pick = bitfieldExtract(draw.gpufx_used[2], 18, 4);
        if (pick != 0u) {
            alpha_scale = alpha;
            if (pick == 1u) {
                x = clamp(sqrt(vector.y * vector.y + vector.x * vector.x), 0.0, 1.0);
            } else if (pick == 2u) {
                x = fade;
                fade_out = 1.0;
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
    alpha = fade_out * alpha;
    if (uintBitsToFloat(0x34000000u) > alpha) {
        discard;
    }
    alpha = draw.constants[MaterialAttr2 + 3u] * alpha;
    // Above a threshold of 0 the alpha below it goes, and where the dissolve's full mark reaches
    // 1 - 2^-23 the rest is whole.
    if (0.0 < threshold.x) {
        alpha = alpha < threshold.x
                    ? 0.0
                    : (uintBitsToFloat(0x3f7ffffeu) <= threshold.y ? 1.0 : alpha);
    }
    if (uintBitsToFloat(0x34000000u) > alpha) {
        discard;
    }
    const vec3 premultiplied = alpha * color;
    const float coverage = clamp((1.0 - draw.constants[MaterialAttr]) * alpha, 0.0, 1.0);
    return vec4(unpackHalf2x16(packHalf2x16(premultiplied.rg)),
                unpackHalf2x16(packHalf2x16(vec2(premultiplied.b, coverage))));
}

// MaterialColor's colour and alpha before the alpha premultiplies them, for the programs that
// fog the colour first.
vec4 MaterialCurved(ParticleDraw draw, vec3 color, float particle_alpha, float fade,
                    vec2 threshold, vec2 vector, float value, uint curve_offset) {
    float alpha = particle_alpha * fade;
    float fade_out = fade;
    const uint curve = bitfieldExtract(draw.gpufx_used[2], 4, 14);
    if (curve > 0u) {
        float x = clamp(max(0.0, alpha), 0.0, 1.0);
        float alpha_scale = 1.0;
        const uint pick = bitfieldExtract(draw.gpufx_used[2], 18, 4);
        if (pick != 0u) {
            alpha_scale = alpha;
            if (pick == 1u) {
                x = clamp(sqrt(vector.y * vector.y + vector.x * vector.x), 0.0, 1.0);
            } else if (pick == 2u) {
                x = fade;
                fade_out = 1.0;
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
    alpha = fade_out * alpha;
    if (uintBitsToFloat(0x34000000u) > alpha) {
        discard;
    }
    alpha = draw.constants[MaterialAttr2 + 3u] * alpha;
    // Above a threshold of 0 the alpha below it goes, and where the dissolve's full mark reaches
    // 1 - 2^-23 the rest is whole.
    if (0.0 < threshold.x) {
        alpha = alpha < threshold.x
                    ? 0.0
                    : (uintBitsToFloat(0x3f7ffffeu) <= threshold.y ? 1.0 : alpha);
    }
    if (uintBitsToFloat(0x34000000u) > alpha) {
        discard;
    }
    return vec4(color, alpha);
}
