// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the game's GPU particle pixel programs read beside particle_pixel.glsl: the particle's
// material (560 bytes in the material buffer), its curves, the dissolve of its alpha by a noise of
// its texture coordinates and a 4x4 ordered dither, and the fade by the depth behind it. Words
// whose names the game does not give are named by what these programs do with them.

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer MaterialWords {
    uint words[];
};

// A material (140 words): the dissolve's threshold and its full mark, its four erosion values,
// the depth fade's scale, the curves that replace those three where they are above 0, and the
// first material of the particle's set.
const uint MaterialWordsEach = 140u;
const uint MaterialDissolve = 0u;
const uint MaterialErosion = 4u;
const uint MaterialFadeScale = 8u;
const uint MaterialDissolveCurve = 120u;
const uint MaterialErosionCurve = 121u;
const uint MaterialFadeCurve = 122u;
const uint MaterialSetFirst = 132u;

// The particle's material: the set at c_gpufxUsed.w plus the set the particle names, then the
// material within that set.
uint GpuMaterial(ParticleDraw draw, uint set, uint within) {
    MaterialWords materials = MaterialWords(material_address);
    return materials.words[((draw.gpufx_used[3] + set) * 560u + 528u) >> 2] + within;
}

uint MaterialWord(uint material, uint word) {
    return MaterialWords(material_address).words[(material * 560u + 4u * word) >> 2];
}

vec4 MaterialVector(uint material, uint word) {
    return uintBitsToFloat(uvec4(MaterialWord(material, word), MaterialWord(material, word + 1u),
                                 MaterialWord(material, word + 2u),
                                 MaterialWord(material, word + 3u)));
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
    const float dither = (1.0 / 16.0) * uintBitsToFloat(DitherTable[(cell << 2) >> 2]);
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
