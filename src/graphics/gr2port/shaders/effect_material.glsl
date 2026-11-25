// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's effect material, the pixel program of effect models and of GPU particle effects: four
// colour maps, each blended into the colour by its own method (colour methods 1 to 12, then the
// depth, the next map's offset, the normal, the background seen through a normal, a stored factor,
// a shift of the texture coordinates, a cut and two lines of colour), then the vertex colour,
// the lights, the alpha's tests and the soft depth. Include particle_lit.glsl first; the
// including program gives EffectConstant(word), EffectMap(map) (the map's texture),
// EffectBackground(place) (the background's colour) and EffectSceneDepth(pixel) (1 less the
// scene's depth at a pixel of the game's size).

// The pixel constants (Buffer1) by word: the game's size and its inverse, per map a blend
// method, colour scale, alpha scale, contrast and texture offset and repeat, the colour scale of
// the output, the alpha reference, the fresnel's borders, line colour 0 with the softness after
// it, line colour 1 with the depth's scale after it, the lines' places and widths, the
// distortion's scale, the light's colour and direction, the alpha's second scale, the inverse
// view's depth terms, the toning, the base colour, the flags, the count of a cell's point lights
// and the light's range.
const uint EffectTargetSize = 0u;
const uint EffectBlendMethod = 4u;
const uint EffectColorScale = 8u;
const uint EffectAlphaScale = 12u;
const uint EffectContrast = 16u;
const uint EffectUVOffset = 20u;
const uint EffectOutputScale = 36u;
const uint EffectAlphaRef = 37u;
const uint EffectFresnelBorder = 38u;
const uint EffectLineColor0 = 40u;
const uint EffectSoftness = 43u;
const uint EffectLineColor1 = 44u;
const uint EffectDepthScale = 47u;
const uint EffectLinePlace = 48u;
const uint EffectLineWidth = 50u;
const uint EffectDistortionScale = 52u;
const uint EffectLightColor = 53u;
const uint EffectLightDirection = 56u;
const uint EffectAlphaScale2 = 59u;
const uint EffectInverseView = 60u;
const uint EffectToning = 62u;
const uint EffectBaseColor = 64u;
const uint EffectFlags = 67u;
const uint EffectPointLights = 68u;
const uint EffectLightRange = 69u;

// u_nFlags: lit, the alpha's step, the alpha test, the fresnel, the soft depth, and per map the
// red taken for green and blue.
const uint EffectFlagLit = 0x20u;
const uint EffectFlagAlphaTest = 0x100u;
const uint EffectFlagFresnel = 0x200u;
const uint EffectFlagAlphaStep = 0x400u;
const uint EffectFlagSoft = 0x8000u;
const uint EffectFlagRed = 0x10000000u;

// What the maps carry from one to the next: the colour and alpha, the normal, the texture
// coordinates and the next map's offset, the depth, a stored factor, the fresnel weight and the
// alpha's second scale (the lines set both to 1).
struct EffectLayers {
    vec3 color;
    float alpha;
    vec3 normal;
    vec2 uv;
    vec2 offset;
    float depth;
    float factor;
    float fresnel;
    float alpha_scale2;
};

// The sign bit flipped as an integer, as the game's program does: a NaN keeps its sign.
float EffectFlippedSign(float x) {
    const uint bits = floatBitsToUint(x);
    return uintBitsToFloat(bitfieldInsert(bits, ~(bits >> 31), 31, 1));
}

// A map's texel at the carried texture coordinates (past map 0 plus the offset), through the map's
// offset and repeat (v from the far edge), with red for green and blue where the flags ask (not
// with EFFECT_NO_RED_FLAGS, the shadow pass's program).
vec4 EffectTexel(EffectLayers layers, uint map, bool first) {
    const uint words = EffectUVOffset + 4u * map;
    const vec2 uv = first ? layers.uv
                          : vec2(layers.offset.x + layers.uv.x, layers.offset.y + layers.uv.y);
    const float u = EffectConstant(words + 2u) * uv.x + EffectConstant(words);
    const float v =
        EffectFlippedSign(EffectConstant(words + 3u) * uv.y + EffectConstant(words + 1u));
    vec4 texel = textureLod(
        sampler2D(textures[nonuniformEXT(EffectMap(map) & TextureSlotMask)], texture_sampler),
        vec2(u, v), 0.0);
#ifndef EFFECT_NO_RED_FLAGS
    if ((floatBitsToUint(EffectConstant(EffectFlags)) & (EffectFlagRed << map)) != 0u) {
        texel.yz = texel.xx;
    }
#endif
    return texel;
}

// The luminance the methods 9 to 12 take.
float EffectLuminance(vec3 c) {
    return c.z * uintBitsToFloat(0x3d834271u) +
           (c.y * uintBitsToFloat(0x3f2b9479u) + uintBitsToFloat(0x3e87b6feu) * c.x);
}

// The map's colour and alpha for the colour methods: the colour scale times the texel raised to
// the contrast, the alpha scale times the texel's alpha.
vec4 EffectColor(vec4 texel, uint map) {
    const float contrast = EffectConstant(EffectContrast + map);
    const float r = exp2(contrast * log2(abs(texel.x)));
    const float g = exp2(contrast * log2(abs(texel.y)));
    const float b = exp2(contrast * log2(abs(texel.z)));
    const float scale = EffectConstant(EffectColorScale + map);
    return vec4(scale * r, scale * g, scale * b, EffectConstant(EffectAlphaScale + map) * texel.w);
}

// Map 0's colour methods 1 to 12 over the base colour with alpha 1 (the game's program folds
// both in).
void EffectFirstColor(inout EffectLayers layers, uint method, vec4 c) {
    const vec3 base = vec3(EffectConstant(EffectBaseColor), EffectConstant(EffectBaseColor + 1u),
                           EffectConstant(EffectBaseColor + 2u));
    const float a = c.w;
    if (method == 1u) {
        const float rest = 1.0 - a;
        layers.color = vec3(base.x * rest + c.x * a, base.y * rest + c.y * a,
                            base.z * rest + c.z * a);
        layers.alpha = a * a + rest;
    } else if (method == 2u) {
        layers.color = vec3(base.x + c.x, base.y + c.y, base.z + c.z);
        layers.alpha = 1.0 + a;
    } else if (method == 3u) {
        layers.color = vec3(base.x * c.x, base.y * c.y, base.z * c.z);
        layers.alpha = a;
    } else if (method == 4u) {
        layers.color = c.xyz;
        layers.alpha = a;
    } else if (method == 5u) {
        const float rest = 1.0 - a;
        layers.color = vec3(base.x * rest + c.x * a, base.y * rest + c.y * a,
                            base.z * rest + c.z * a);
        layers.alpha = 1.0;
    } else if (method == 6u) {
        layers.color = vec3(base.x + c.x, base.y + c.y, base.z + c.z);
        layers.alpha = 1.0;
    } else if (method == 7u) {
        layers.color = vec3(base.x * c.x, base.y * c.y, base.z * c.z);
        layers.alpha = 1.0;
    } else if (method == 8u) {
        layers.color = c.xyz;
        layers.alpha = 1.0;
    } else {
        const float lum = EffectLuminance(c.xyz);
        layers.color = base;
        if (method == 9u) {
            layers.alpha = 1.0 + (lum * lum + -lum);
        } else if (method == 10u) {
            layers.alpha = 1.0 + lum;
        } else {
            layers.alpha = lum;
        }
    }
}

// Maps 1 to 3's colour methods 1 to 12 over the colour and alpha so far.
void EffectNextColor(inout EffectLayers layers, uint method, vec4 c) {
    const vec3 base = layers.color;
    const float base_alpha = layers.alpha;
    const float a = c.w;
    if (method == 1u) {
        const float rest = 1.0 - a;
        layers.color = vec3(base.x * rest + c.x * a, base.y * rest + c.y * a,
                            base.z * rest + c.z * a);
        layers.alpha = base_alpha * rest + a * a;
    } else if (method == 2u) {
        layers.color = vec3(base.x + c.x, base.y + c.y, base.z + c.z);
        layers.alpha = base_alpha + a;
    } else if (method == 3u) {
        layers.color = vec3(base.x * c.x, base.y * c.y, base.z * c.z);
        layers.alpha = base_alpha * a;
    } else if (method == 4u) {
        layers.color = c.xyz;
        layers.alpha = a;
    } else if (method == 5u) {
        const float rest = 1.0 - a;
        layers.color = vec3(base.x * rest + c.x * a, base.y * rest + c.y * a,
                            base.z * rest + c.z * a);
    } else if (method == 6u) {
        layers.color = vec3(base.x + c.x, base.y + c.y, base.z + c.z);
    } else if (method == 7u) {
        layers.color = vec3(base.x * c.x, base.y * c.y, base.z * c.z);
    } else if (method == 8u) {
        layers.color = c.xyz;
    } else if (method == 9u) {
        const float lum = EffectLuminance(c.xyz);
        layers.alpha = lum * -base_alpha + (lum * lum + base_alpha);
    } else if (method == 10u) {
        layers.alpha = c.z * uintBitsToFloat(0x3d834271u) +
                       (c.y * uintBitsToFloat(0x3f2b9479u) +
                        (c.x * uintBitsToFloat(0x3e87b6feu) + base_alpha));
    } else if (method == 11u) {
        layers.alpha = base_alpha * EffectLuminance(c.xyz);
    } else {
        layers.alpha = EffectLuminance(c.xyz);
    }
}

// What the methods past the colours read of the pixel: its place in the game's pixels, its depth
// (1 - z), its world place's x and y times 100, the tangent frame's tangent, the tangent's w and
// its sign.
struct EffectPixel {
    vec2 place;
    float depth;
    vec2 world;
    vec3 tangent;
    float tangent_w;
    float side;
};

// A tangent-space normal from a texel (2 rgb - 1, unit length, times the colour scale).
vec3 EffectTexelNormal(vec4 texel, float scale) {
    const float x = 2.0 * texel.x + -1.0;
    const float y = 2.0 * texel.y + -1.0;
    const float z = 2.0 * texel.z + -1.0;
    const float k = scale * (1.0 / sqrt(z * z + (y * y + x * x)));
    return vec3(k * x, k * y, k * z);
}

// Method 15: the normal bent by the texel's normal through the tangent frame (bitangent n x t
// times the tangent's sign), unit length.
vec3 EffectBentNormal(vec3 n, vec3 t, float side, vec3 bend) {
    const float bx = side * (t.y * -n.z + n.y * t.z);
    const float by = side * (t.z * -n.x + n.z * t.x);
    const float bz = side * (t.x * -n.y + n.x * t.y);
    const float x = bend.z * n.x + (bx * bend.y + t.x * bend.x);
    const float y = bend.z * n.y + (bend.y * by + t.y * bend.x);
    const float z = bend.z * n.z + (bend.y * bz + t.z * bend.x);
    const float k = 1.0 / sqrt(z * z + (y * y + x * x));
    return vec3(k * x, k * y, k * z);
}

// Method 16: the background seen through the texel's normal: the normal's view x and y (the
// bitangent times the tangent's w) move the pixel by the distortion's scale.
vec3 EffectDistorted(EffectPixel pixel, vec3 n, vec3 bend) {
    const vec3 t = pixel.tangent;
    const float bx = pixel.tangent_w * (t.y * -n.z + n.y * t.z);
    const float by = pixel.tangent_w * (t.z * -n.x + n.z * t.x);
    const float x = bend.z * n.x + (bend.y * bx + t.x * bend.x);
    const float y = bend.z * n.y + (bend.y * by + t.y * bend.x);
    const float scale = EffectConstant(EffectDistortionScale);
    const vec2 at = vec2(EffectConstant(EffectTargetSize + 2u) * (scale * x + pixel.place.x),
                         EffectConstant(EffectTargetSize + 3u) * (scale * -y + pixel.place.y));
    return EffectBackground(at);
}

// The texel raised to the map's contrast, times its colour scale (x and y).
vec2 EffectRaised(vec4 texel, uint map) {
    const float contrast = EffectConstant(EffectContrast + map);
    const float scale = EffectConstant(EffectColorScale + map);
    return vec2(scale * exp2(contrast * log2(abs(texel.x))),
                scale * exp2(contrast * log2(abs(texel.y))));
}

// Methods 13 to 20 (and none): map 0 starts them from the base colour with alpha 1, the depth
// 1 - z and no factor; every map starts with no offset.
void EffectOtherMethod(inout EffectLayers layers, uint map, uint method, vec4 texel,
                       EffectPixel pixel, bool first) {
    if (first) {
        layers.color = vec3(EffectConstant(EffectBaseColor), EffectConstant(EffectBaseColor + 1u),
                            EffectConstant(EffectBaseColor + 2u));
        layers.alpha = 1.0;
        layers.depth = pixel.depth;
        layers.factor = 0.0;
    }
    layers.offset = vec2(0.0);
    const float contrast = EffectConstant(EffectContrast + map);
    const float scale = EffectConstant(EffectColorScale + map);
    const float alpha_scale = EffectConstant(EffectAlphaScale + map);
    if (method == 13u) {
        // The depth moved by the depth's scale less the raised red times it.
        const float depth_scale = EffectConstant(EffectDepthScale);
        layers.depth = exp2(contrast * log2(abs(texel.x))) * (-scale * depth_scale) +
                       (depth_scale + layers.depth);
    } else if (method == 14u) {
        // The next map's offset: twice the raised texel plus the coordinates, less 1.
        const vec2 raised = EffectRaised(texel, map);
        layers.offset = vec2(-1.0 + (2.0 * raised.x + layers.uv.x),
                             -1.0 + (2.0 * raised.y + layers.uv.y));
    } else if (method == 15u) {
        layers.normal = EffectBentNormal(layers.normal, pixel.tangent, pixel.side,
                                         EffectTexelNormal(texel, scale));
    } else if (method == 16u) {
        layers.color = EffectDistorted(pixel, layers.normal, EffectTexelNormal(texel, scale));
        layers.alpha = 1.0;
    } else if (method == 17u) {
        layers.factor = scale * exp2(contrast * log2(abs(texel.x)));
    } else if (method == 18u) {
        // The coordinates of the later maps moved by twice the raised texel, less 1.
        const vec2 raised = EffectRaised(texel, map);
        layers.uv = vec2(-1.0 + (2.0 * raised.x + layers.uv.x),
                         -1.0 + (2.0 * raised.y + layers.uv.y));
    } else if (method == 19u) {
        // A cut: the map read at the world place and the pixel scaled down; the pixel goes where
        // the alpha scale times the alpha and the fresnel weight does not stay below it.
        const vec2 at = vec2(scale * (0.01 * pixel.place.x) + pixel.world.x,
                             scale * (0.01 * pixel.place.y) + pixel.world.y);
        const float edge = (alpha_scale * layers.alpha) * layers.fresnel;
        layers.fresnel = 1.0;
        const float cut =
            textureLod(sampler2D(textures[nonuniformEXT(EffectMap(map) & TextureSlotMask)],
                                 texture_sampler),
                       at, 0.0)
                .x;
        if (edge <= cut) {
            discard;
        }
    } else if (method == 20u) {
        // Two lines: the raised luminance times the scales, the alpha and the fresnel weight
        // picks line colour 0 or 1 where it lies in a line's width past its place and the alpha's
        // reference; the alpha is that value, the fresnel weight and the second alpha scale 1.
        // Map 0's program folds its alpha of 1 in, with the scales in another order.
        const float r = exp2(contrast * log2(abs(texel.x)));
        const float g = exp2(contrast * log2(abs(texel.y)));
        const float b = exp2(contrast * log2(abs(texel.z)));
        const float lum = b * uintBitsToFloat(0x3d834271u) +
                          (g * uintBitsToFloat(0x3f2b9479u) + uintBitsToFloat(0x3e87b6feu) * r);
        const float scales = first
                                 ? alpha_scale * (layers.alpha_scale2 * scale)
                                 : layers.alpha_scale2 * ((scale * alpha_scale) * layers.alpha);
        const float q = (scales * layers.fresnel) * lum;
        layers.alpha = q;
        const float ref = EffectConstant(EffectAlphaRef);
        const float low0 = EffectConstant(EffectLinePlace) + ref;
        if (low0 <= q && q < EffectConstant(EffectLineWidth) + low0) {
            layers.color = vec3(EffectConstant(EffectLineColor0),
                                EffectConstant(EffectLineColor0 + 1u),
                                EffectConstant(EffectLineColor0 + 2u));
        } else {
            const float low1 = EffectConstant(EffectLinePlace + 1u) + ref;
            if (low1 <= q && q < EffectConstant(EffectLineWidth + 1u) + low1) {
                layers.color = vec3(EffectConstant(EffectLineColor1),
                                    EffectConstant(EffectLineColor1 + 1u),
                                    EffectConstant(EffectLineColor1 + 2u));
            }
        }
        layers.fresnel = 1.0;
        layers.alpha_scale2 = 1.0;
    }
}

// Map `map` (0 to 3) through its blend method (word 4 + map).
void EffectLayer(inout EffectLayers layers, uint map, EffectPixel pixel, bool first) {
    const vec4 texel = EffectTexel(layers, map, first);
    const int method = int(floatBitsToUint(EffectConstant(EffectBlendMethod + map)));
    if (0 < method && method < 13) {
        if (first) {
            EffectFirstColor(layers, uint(method), EffectColor(texel, map));
            layers.depth = pixel.depth;
            layers.factor = 0.0;
        } else {
            EffectNextColor(layers, uint(method), EffectColor(texel, map));
        }
        layers.offset = vec2(0.0);
    } else {
        EffectOtherMethod(layers, map, uint(method), texel, pixel, first);
    }
}

// The fresnel weight where the flags ask, else 1: the smooth step 3t^2 - 2t^3 of the normal's |z|
// from the fresnel's low border to its high.
float EffectFresnel(float normal_z) {
    float weight = 1.0;
    if ((floatBitsToUint(EffectConstant(EffectFlags)) & EffectFlagFresnel) != 0u) {
        const float low = EffectConstant(EffectFresnelBorder);
        const float r = 1.0 / (EffectConstant(EffectFresnelBorder + 1u) - low);
        const float t = clamp(-low * r + abs(normal_z) * r, 0.0, 1.0);
        const float square = t * t;
        weight = -2.0 * (t * square) + 3.0 * square;
    }
    return weight;
}

// The layers before map 0: the unit normal with its fresnel weight, the texture coordinates and
// the second alpha scale. Map 0 sets the rest.
EffectLayers EffectStart(vec3 normal, vec2 uv) {
    EffectLayers layers;
    const float k = 1.0 / sqrt(normal.z * normal.z + (normal.y * normal.y + normal.x * normal.x));
    layers.normal = vec3(normal.x * k, normal.y * k, normal.z * k);
    layers.fresnel = EffectFresnel(layers.normal.z);
    layers.uv = uv;
    layers.offset = vec2(0.0);
    layers.alpha_scale2 = EffectConstant(EffectAlphaScale2);
    return layers;
}

// The pixel at its place in the game's pixels, with its tangent (w its sign) and its world
// place's x and y.
EffectPixel EffectPixelAt(vec2 place, vec4 tangent, vec2 world) {
    EffectPixel pixel;
    pixel.place = place;
    pixel.depth = 1.0 - gl_FragCoord.z;
    pixel.world = vec2(100.0 * world.x, 100.0 * world.y);
    const float k =
        1.0 / sqrt(tangent.z * tangent.z + (tangent.y * tangent.y + tangent.x * tangent.x));
    pixel.tangent = vec3(tangent.x * k, tangent.y * k, tangent.z * k);
    pixel.tangent_w = tangent.w;
    pixel.side = Handedness(tangent.w);
    return pixel;
}

// The first light index of the pixel's cell of LightCompute's grid: the screen's 60x36 tile at the
// place times the inverse of the game's size, in the slice of its view depth (through the inverse
// view's depth terms) past the near plane, 48 indices a cell.
uint EffectLightCell(EffectPixel pixel) {
    const float scale = EffectConstant(EffectInverseView);
    const float offset = EffectConstant(EffectInverseView + 1u);
    const float ndc = 2.0 * pixel.depth + -1.0;
    const float depth = 1.0 / (scale * ndc + offset) - 1.0 / (offset - scale);
    const uint slice = uint(clamp(int(LightUint(floor(log2(depth)))), 0, 7));
    const uint row = LightUint(36.0 * (EffectConstant(EffectTargetSize + 3u) * pixel.place.y));
    const uint column = LightUint(60.0 * (EffectConstant(EffectTargetSize + 2u) * pixel.place.x));
    return 48u * (column + 60u * row + 2160u * slice);
}

// The light on the pixel: the directional light wrapped by its range on both sides (the far side
// half the range at most, the near side times the stored factor), with EFFECT_HALF_LAMBERT (the
// particle program) as (0.5 w + 0.5)^2, then each of the cell's point lights at the view place
// wrapped the same way, less its fade. The colours, each light's colour times its part, come back
// in xyz and the parts' sum in w.
vec4 EffectLight(EffectLayers layers, EffectPixel pixel, vec3 view) {
    const vec3 n = layers.normal;
    const vec3 direction = vec3(EffectConstant(EffectLightDirection),
                                EffectConstant(EffectLightDirection + 1u),
                                EffectConstant(EffectLightDirection + 2u));
    const float cosine =
        (1.0 / sqrt(direction.z * direction.z +
                    (direction.y * direction.y + direction.x * direction.x))) *
        (direction.z * n.z + (direction.y * n.y + direction.x * n.x));
    const float range = EffectConstant(EffectLightRange);
    const float base = 0.5 * range;
    const float scale = 0.5 * -range + 1.0;
    const float far_side = max(0.0, scale * -cosine + base);
#ifdef EFFECT_HALF_LAMBERT
    const float wrapped = 0.5 * (max(0.0, cosine) * layers.factor + far_side) + 0.5;
    float sum = wrapped * wrapped;
#else
    float sum = max(0.0, cosine) * layers.factor + far_side;
#endif
    vec3 colors = vec3(EffectConstant(EffectLightColor) * sum,
                       EffectConstant(EffectLightColor + 1u) * sum,
                       EffectConstant(EffectLightColor + 2u) * sum);
    const uint first = EffectLightCell(pixel);
    const uint count = floatBitsToUint(EffectConstant(EffectPointLights));
    // A cell holds 48 entries, the last after its lights ~0.
    for (uint k = 0u; k < min(count, 48u); k++) {
        const uint index = CellLight(first, k);
        if (index == 0xffffffffu) {
            break;
        }
        const PointLight point = ReadPointLight(index);
        const vec3 to_light = point.position - view;
        const float distance_squared =
            to_light.z * to_light.z + (to_light.y * to_light.y + to_light.x * to_light.x);
        const float faded = PointLightFaded(point, sqrt(distance_squared));
        const float facing =
            max(0.0, (scale * (1.0 / sqrt(distance_squared))) *
                             (to_light.z * n.z + (to_light.y * n.y + n.x * to_light.x)) +
                         base);
        const float lit = facing * -faded + facing;
        sum = sum + lit;
        colors = lit * point.color + colors;
    }
    return vec4(colors, sum);
}

// The colour lit: with a toning above 1 its luma times the toning and the light's sum, floored and
// over the toning, back to red, green and blue with the colour's chroma (held to 0 to 1); else
// the colour times the light's sum. Either way times the light's colours (at least 0).
vec3 EffectLit(vec3 color, vec4 light) {
    const float toning = EffectConstant(EffectToning);
    const vec3 colors = vec3(max(0.0, light.x), max(0.0, light.y), max(0.0, light.z));
    if (1.0 < toning) {
        const float luma = color.b * uintBitsToFloat(0x3de978d5u) +
                           (color.g * uintBitsToFloat(0x3f1645a2u) +
                            uintBitsToFloat(0x3e991687u) * color.r);
        const float stepped = floor((toning * light.w) * luma) * (1.0 / toning);
        const float cb = 0.5 * color.b + (color.g * uintBitsToFloat(0xbea99ae9u) +
                                          uintBitsToFloat(0xbe2cca2eu) * color.r);
        const float cr = color.b * uintBitsToFloat(0xbda685dbu) +
                         (color.g * uintBitsToFloat(0xbed65e89u) + 0.5 * color.r);
        const float g = clamp(uintBitsToFloat(0x3f36d1e1u) * -cr +
                                  (uintBitsToFloat(0x3eb0331eu) * -cb + stepped),
                              0.0, 1.0);
        const float r = clamp(uintBitsToFloat(0x3fb374bcu) * cr + stepped, 0.0, 1.0);
        const float b = clamp(uintBitsToFloat(0x3fe2d0e5u) * cb + stepped, 0.0, 1.0);
        return vec3(colors.x * r, colors.y * g, colors.z * b);
    }
    return vec3((color.r * light.w) * colors.x, (color.g * light.w) * colors.y,
                (color.b * light.w) * colors.z);
}

// The pixel's colour and alpha after the maps: the colour held to 0 to 1 times the vertex colour,
// lit at the view place where the flags ask; the alpha held to 0 to 1 times the vertex alpha, the
// second alpha scale and the fresnel weight, stepped to 0 or 1 at the reference and tested
// against it (the pixel goes where it does not pass) where the flags ask; with the soft depth,
// where the scene lies within the softness behind the pixel the alpha falls with the distance to
// it, and the pixel goes where the scene is not behind it.
vec4 EffectFinish(EffectLayers layers, EffectPixel pixel, vec4 vertex_color, vec3 view) {
    const uint flags = floatBitsToUint(EffectConstant(EffectFlags));
    vec3 color = vec3(vertex_color.x * clamp(max(0.0, layers.color.x), 0.0, 1.0),
                      vertex_color.y * clamp(max(0.0, layers.color.y), 0.0, 1.0),
                      vertex_color.z * clamp(max(0.0, layers.color.z), 0.0, 1.0));
    if ((flags & EffectFlagLit) != 0u) {
        color = EffectLit(color, EffectLight(layers, pixel, view));
    }
    const float held = vertex_color.w * clamp(max(0.0, layers.alpha), 0.0, 1.0);
    float alpha = (layers.alpha_scale2 * held) * layers.fresnel;
    const float reference = EffectConstant(EffectAlphaRef);
    if ((flags & EffectFlagAlphaStep) != 0u) {
        alpha = reference <= alpha ? 1.0 : 0.0;
    }
    if ((flags & EffectFlagAlphaTest) != 0u && reference >= alpha) {
        discard;
    }
    if ((flags & EffectFlagSoft) != 0u) {
        const float scale = EffectConstant(EffectInverseView);
        const float offset = EffectConstant(EffectInverseView + 1u);
        const float own = 1.0 / (scale * (2.0 * layers.depth + -1.0) + offset);
        const float scene = EffectSceneDepth(
            ivec2(int(LightUint(pixel.place.x)), int(LightUint(pixel.place.y))));
        const float behind = 1.0 / (scale * (2.0 * scene + -1.0) + offset) - own;
        const float softness = EffectConstant(EffectSoftness);
        if (softness > behind) {
            if (0.0 >= behind) {
                discard;
            }
            alpha = (behind * alpha) * (1.0 / softness);
        }
    }
    return vec4(color, alpha);
}
