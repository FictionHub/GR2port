// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the game's sprite and model particle pixel programs share beside particle_pixel.glsl: the
// four maps in the game's T0 to T3, each moved by the maps before it, the combiner that mixes them
// by c_gpufxUsed's fields, and the end that colours and fades the result by the particle's curve.
// Include particle_gpu_push.glsl first: the material lies at push constant slot 5.

#extension GL_EXT_nonuniform_qualifier : require

#include "particle_pixel.glsl"

layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler texture_sampler;

// The handles of the game's T0 to T3, at byte 352 of the ParticleDraw, after the constants.
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer CombinerTextures {
    uint handles[4];
};
const uint64_t CombinerTexturesOffset = 352ul;

// The materials, 560 bytes each; a material's words 4k to 4k + 3 hold how far map k moves the
// coordinates of maps 0 to 3 that follow it.
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer CombinerMaterials {
    float words[];
};

// Word `column` of row `row` of a material's strengths, addressed in 32 bits as the game's
// programs do.
float MoveStrength(uint material, uint row, uint column) {
    return CombinerMaterials(material_address).words[((material * 560u + 16u * row) >> 2) + column];
}

// The game's programs clear the lowest bit of a texture coordinate's x before reading a map.
float LowBitCleared(float x) {
    return uintBitsToFloat(floatBitsToUint(x) & ~1u);
}

// A map at the place with a bias of 0. Where its bit of c_gpufxUsed.z (3 for T0 down to 0 for
// T3) is set, the map holds only its w, in red, and its x, y and z are 1.
vec4 CombinerMap(ParticleDraw draw, uint map, vec2 place) {
    const uint handle = CombinerTextures(draws_address + uint64_t(draw_index) * ParticleDrawBytes +
                                         CombinerTexturesOffset)
                            .handles[map];
    const vec2 at = vec2(LowBitCleared(place.x), place.y);
    vec4 color;
    if (bitfieldExtract(draw.gpufx_used[2], 3 - int(map), 1) != 0u) {
        color = vec4(1.0, 1.0, 1.0,
                     texture(sampler2D(textures[nonuniformEXT(handle)], texture_sampler), at, 0.0)
                         .x);
    } else {
        color = texture(sampler2D(textures[nonuniformEXT(handle)], texture_sampler), at, 0.0);
    }
    return color;
}

// A layer's luminance (Rec. 709) times its alpha.
float LuminanceAlpha(vec4 color) {
    return color.a * fma(color.b, uintBitsToFloat(0x3d93dd98u),
                         fma(color.g, uintBitsToFloat(0x3f371759u),
                             uintBitsToFloat(0x3e59b3d0u) * color.r));
}

// The four ways a combiner stage takes a layer, by c_gpufxUsed.y's fields: its luminance times
// alpha everywhere; its colour with alpha 1; its colour and alpha; white with its alpha. x keeps
// the colour, y keeps the alpha, z puts the luminance times alpha in all four.
const vec4 OperandWays[4] = vec4[4](vec4(0.0, 0.0, 1.0, 0.0), vec4(1.0, 0.0, 0.0, 0.0),
                                    vec4(1.0, 1.0, 0.0, 0.0), vec4(0.0, 1.0, 0.0, 0.0));

// A layer taken by a stage, in the order of operations of the game's programs.
vec4 Operand(vec4 layer, float luminance_alpha, vec4 way) {
    const float red = 1.0 + (way.x * layer.r + -way.x);
    const float green = way.x * layer.g + (1.0 - way.x);
    const float blue = way.x * layer.b + (1.0 - way.x);
    const float alpha = 1.0 + (layer.a * way.y + -way.y);
    const float keep = 1.0 - way.z;
    const float share = way.z * luminance_alpha;
    return vec4(way.z * luminance_alpha + (-way.z * red + red), fma(keep, green, share),
                fma(keep, blue, share), fma(keep, alpha, share));
}

// The eight layers a combiner works on: the four maps, then the three stages' results; the last
// is white.
struct Layers {
    vec4 color[8];
    float luminance_alpha[8];
};

// One stage: c_gpufxUsed.x picks the two layers from bit a and bit a - 3, c_gpufxUsed.y how each is
// taken from bit b and bit b - 3, and from bit b - 6 how they mix: 1 the first over the second by
// its alpha, 2 the first times its alpha plus the second, 3 their product, else white. The result,
// clamped to 0 to 1, becomes layer `into`.
void CombinerStage(ParticleDraw draw, inout Layers layers, int a, int b, uint into) {
    const uint first = bitfieldExtract(draw.gpufx_used[0], a, 3);
    const uint second = bitfieldExtract(draw.gpufx_used[0], a - 3, 3);
    const vec4 x = Operand(layers.color[first], layers.luminance_alpha[first],
                           OperandWays[bitfieldExtract(draw.gpufx_used[1], b, 3)]);
    const vec4 y = Operand(layers.color[second], layers.luminance_alpha[second],
                           OperandWays[bitfieldExtract(draw.gpufx_used[1], b - 3, 3)]);
    const uint mix = bitfieldExtract(draw.gpufx_used[1], b - 6, 3);
    vec4 result = vec4(1.0);
    if (mix == 1u) {
        const float rest = 1.0 - x.a;
        result = vec4(fma(vec3(rest), y.rgb, x.rgb * x.a), y.a * rest + x.a);
    } else if (mix == 2u) {
        result = vec4(x.rgb * x.a + y.rgb, x.a + y.a);
    } else if (mix == 3u) {
        result = x * y;
    }
    result = clamp(max(vec4(0.0), result), 0.0, 1.0);
    layers.color[into] = result;
    layers.luminance_alpha[into] = LuminanceAlpha(result);
}

// The particle's colour and alpha from its maps: map 0 at the first coordinates, each further map
// at its own moved by the maps before it (2 * red and green - 1 times the material's strengths),
// mixed by three stages; c_gpufxUsed.x's bits 27 and 24 pick the layers of the colour and the
// alpha (7 for white), c_gpufxUsed.z's bits 31 and 30 add the colour layer's colour and its
// luminance times alpha, bits 29 and 28 the alpha layer's alpha and its luminance times alpha.
vec4 CombinedMaps(ParticleDraw draw, uint material_index, vec2 place0, vec2 place1, vec2 place2,
                  vec2 place3) {
    const uint material = draw.gpufx_used[3] + material_index;
    Layers layers;
    layers.color[0] = CombinerMap(draw, 0u, place0);
    layers.luminance_alpha[0] = LuminanceAlpha(layers.color[0]);
    const vec2 move0 = 2.0 * layers.color[0].rg + -1.0;
    layers.color[1] = CombinerMap(draw, 1u, move0 * MoveStrength(material, 0u, 1u) + place1);
    layers.luminance_alpha[1] = LuminanceAlpha(layers.color[1]);
    const vec2 move1 = 2.0 * layers.color[1].rg + -1.0;
    layers.color[2] =
        CombinerMap(draw, 2u, move1 * MoveStrength(material, 1u, 2u) +
                                      (MoveStrength(material, 0u, 2u) * move0 + place2));
    layers.luminance_alpha[2] = LuminanceAlpha(layers.color[2]);
    const vec2 move2 = 2.0 * layers.color[2].rg + -1.0;
    layers.color[3] = CombinerMap(
        draw, 3u,
        MoveStrength(material, 2u, 3u) * move2 +
            (move1 * MoveStrength(material, 1u, 3u) +
             (move0 * MoveStrength(material, 0u, 3u) + place3)));
    layers.luminance_alpha[3] = LuminanceAlpha(layers.color[3]);
    for (uint k = 4u; k < 8u; k++) {
        layers.color[k] = vec4(1.0);
        layers.luminance_alpha[k] = 1.0;
    }
    CombinerStage(draw, layers, 15, 27, 4u);
    CombinerStage(draw, layers, 9, 18, 5u);
    CombinerStage(draw, layers, 3, 9, 6u);

    vec3 color = vec3(1.0);
    const uint color_layer = bitfieldExtract(draw.gpufx_used[0], 27, 3);
    if (color_layer < 7u) {
        const float base = layers.luminance_alpha[color_layer] *
                           float(bitfieldExtract(draw.gpufx_used[2], 30, 1));
        color = fma(layers.color[color_layer].rgb,
                    vec3(float(bitfieldExtract(draw.gpufx_used[2], 31, 1))), vec3(base));
    }
    float alpha = 1.0;
    const uint alpha_layer = bitfieldExtract(draw.gpufx_used[0], 24, 3);
    if (alpha_layer < 7u) {
        alpha = fma(layers.luminance_alpha[alpha_layer],
                    float(bitfieldExtract(draw.gpufx_used[2], 28, 1)),
                    layers.color[alpha_layer].a *
                        float(bitfieldExtract(draw.gpufx_used[2], 29, 1)));
    }
    return vec4(color, alpha);
}

// The colour and alpha faded by the depth behind the particle and coloured by its curve, the
// alpha discarded below 2^-23 before and after c_materialAttr2.w. c_gpufxUsed.z holds the
// particle's curve from bit 4, 0 for none, and from bit 18 what picks the place on it: 0 the
// alpha, 1 the vector's length, 2 the fade, 3 the value in thousandths.
vec4 CurvedColor(ParticleDraw draw, vec3 color, float particle_alpha, float fade, vec2 vector,
                 float value, uint curve_offset) {
    float alpha = particle_alpha * fade;
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
    return vec4(color, alpha);
}

// The colour premultiplied by the alpha, with alpha times 1 - c_materialAttr.x, as halves.
vec4 Premultiplied(ParticleDraw draw, vec3 color, float alpha) {
    const vec3 premultiplied = color * alpha;
    const float coverage = clamp((1.0 - draw.constants[MaterialAttr]) * alpha, 0.0, 1.0);
    return vec4(unpackHalf2x16(packHalf2x16(premultiplied.rg)),
                unpackHalf2x16(packHalf2x16(vec2(premultiplied.b, coverage))));
}

// CurvedColor premultiplied.
vec4 CombinedColor(ParticleDraw draw, vec3 color, float particle_alpha, float fade, vec2 vector,
                   float value, uint curve_offset) {
    const vec4 curved = CurvedColor(draw, color, particle_alpha, fade, vector, value, curve_offset);
    return Premultiplied(draw, curved.rgb, curved.a);
}

// The G-buffer's normal of the particle's normal: x and y over their length, times
// sqrt(|0.5 - 0.5 z|).
vec2 GBufferNormal(vec3 normal) {
    const float inverse =
        1.0 / sqrt(max(uintBitsToFloat(0x358637bdu), normal.y * normal.y + normal.x * normal.x));
    const float scale = sqrt(abs(-0.5 * normal.z + 0.5));
    return vec2((inverse * normal.x) * scale, (inverse * normal.y) * scale);
}
