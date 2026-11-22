// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's character glass pixel program 0x1ba747c: the light through the shadow cascade the
// pixel's depth falls in, the normal map turned by the tangent frame, the reflection map moved by
// that normal, a highlight where the light crosses the normal map's alpha, and the colour map,
// each scaled by the program's constants.
#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

// Scene::CharacterGlassDraw: 80 words of vertex constants, 28 of pixel constants, then the mesh,
// whose maps hold the colour, normal and reflection maps.
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer GlassConstants {
    float constants[28];
};
layout(buffer_reference, std430, buffer_reference_align = 8) readonly buffer EnemyMesh {
    uint64_t vertices;
    uint64_t skin;
    uint64_t palette;
    uint stride;
    uint displacement;
    uint elements[16];
    uint maps[8];
};
const uint64_t GlassDrawBytes = 560ul;
const uint64_t GlassPixelOffset = 320ul;
const uint64_t GlassMeshOffset = 432ul;
const uint ColorMap = 0u;
const uint NormalMap = 1u;
const uint ReflectionMap = 2u;

layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler texture_sampler;
// The first two shadow cascades' layers and the last cascade's map, with the compare sampler.
layout(set = 1, binding = 0) uniform sampler2DArrayShadow shadow_map;
layout(set = 1, binding = 2) uniform sampler2DShadow last_shadow_map;
layout(push_constant) uniform Push {
    uint64_t draws_address;
};
layout(location = 15) flat in uint draw_index;

// The unit tangent and its sign; the places in cascades 0, 1 and 2; the unit normal; the texture
// coordinates in z and w.
layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 2) in vec4 attr2;
layout(location = 3) in vec4 attr3;
layout(location = 4) in vec4 attr4;
layout(location = 5) in vec4 attr5;
layout(location = 0) out vec4 color0;

// The pixel constants, Buffer0 in the program's metadata: u_f4ShadowSplitZ, u_f4LightColor,
// u_f4LightDirection, u_f4ColorScale, u_f4ReflectionTecoord (offset, then repeat),
// u_fNormalScale, u_fReflectionIntensity, u_fReflectionDistortion, and the highlight's
// intensity, border, smooth and fresnel.
const uint ShadowSplitZ = 0u;
const uint LightColor = 4u;
const uint LightDirection = 8u;
const uint ColorScale = 12u;
const uint ReflectionTexcoord = 16u;
const uint NormalScale = 20u;
const uint ReflectionIntensity = 21u;
const uint ReflectionDistortion = 22u;
const uint HighlightIntensity = 23u;
const uint HighlightBorder = 24u;
const uint HighlightSmooth = 25u;
const uint HighlightFresnel = 26u;

// The game's tap offsets in cascades 0 and 1, as bits.
const uvec2 Cascade0Taps[9] = uvec2[](
    uvec2(0x3a303ad2u, 0x3a497054u), uvec2(0x39ae5edeu, 0x399e2617u),
    uvec2(0xb992c7e4u, 0x3a6293e4u), uvec2(0xb96530e5u, 0x390837acu),
    uvec2(0xba3574deu, 0x39f12fdeu), uvec2(0xba890a20u, 0x366128dau),
    uvec2(0xb966f641u, 0xba2ab7bcu), uvec2(0x3957bbadu, 0xb9892136u),
    uvec2(0x3a466bc7u, 0xb8bc47dcu));
const uvec2 Cascade1Taps[4] = uvec2[](
    uvec2(0x39087ffbu, 0x39bec07cu), uvec2(0x39ba9634u, 0x38d3d71du),
    uvec2(0xb96af366u, 0x38a6fec7u), uvec2(0x3953b3feu, 0xb9dd771bu));

// A tap of a cascade's layer: the place over its w moved by the offset, its depth compared.
float CascadeTap(vec4 place, float inverse_w, uvec2 offset, float cascade_layer) {
    const vec2 moved = vec2(place.x * inverse_w + uintBitsToFloat(offset.x),
                            place.y * inverse_w + uintBitsToFloat(offset.y));
    return texture(shadow_map, vec4(moved, cascade_layer, place.z * inverse_w));
}

// Cascade 0's nine taps averaged, summed in the game's program's order.
float Cascade0(vec4 place) {
    const float inverse_w = 1.0 / place.w;
    const float first = CascadeTap(place, inverse_w, Cascade0Taps[0], 0.0);
    float sum = CascadeTap(place, inverse_w, Cascade0Taps[1], 0.0) + first;
    sum = sum + CascadeTap(place, inverse_w, Cascade0Taps[2], 0.0);
    sum = sum + CascadeTap(place, inverse_w, Cascade0Taps[3], 0.0);
    sum = sum + CascadeTap(place, inverse_w, Cascade0Taps[4], 0.0);
    sum = sum + CascadeTap(place, inverse_w, Cascade0Taps[5], 0.0);
    sum = sum + CascadeTap(place, inverse_w, Cascade0Taps[6], 0.0);
    sum = sum + CascadeTap(place, inverse_w, Cascade0Taps[7], 0.0);
    sum = sum + CascadeTap(place, inverse_w, Cascade0Taps[8], 0.0);
    return (1.0 / 9.0) * sum;
}

// Cascade 1's four taps averaged.
float Cascade1(vec4 place) {
    const float inverse_w = 1.0 / place.w;
    const float first = CascadeTap(place, inverse_w, Cascade1Taps[0], 1.0);
    float sum = CascadeTap(place, inverse_w, Cascade1Taps[1], 1.0) + first;
    sum = sum + CascadeTap(place, inverse_w, Cascade1Taps[2], 1.0);
    sum = sum + CascadeTap(place, inverse_w, Cascade1Taps[3], 1.0);
    return 0.25 * sum;
}

// The last cascade's one tap.
float LastCascade(vec4 place) {
    const float inverse_w = 1.0 / place.w;
    return texture(last_shadow_map, place.xyz * inverse_w);
}

// The vector over its length, in the order of operations of the game's programs.
vec3 Normalized(vec3 v) {
    return v * (1.0 / sqrt(v.z * v.z + (v.y * v.y + v.x * v.x)));
}

void main() {
    const uint64_t draw = draws_address + uint64_t(draw_index) * GlassDrawBytes;
    GlassConstants pixel = GlassConstants(draw + GlassPixelOffset);
    EnemyMesh mesh = EnemyMesh(draw + GlassMeshOffset);
    // Cascade 0 is nearer than the split's y, cascade 1 nearer than its z.
    const float depth = 1.0 / gl_FragCoord.w;
    float shadow;
    if (pixel.constants[ShadowSplitZ + 1u] > depth) {
        shadow = Cascade0(attr1);
    } else if (pixel.constants[ShadowSplitZ + 2u] > depth) {
        shadow = Cascade1(attr2);
    } else {
        shadow = LastCascade(attr3);
    }

    // The normal map turned by the tangent frame, the bitangent the normal crossed with the
    // tangent, times the tangent's sign.
    const vec2 texcoord = attr5.zw;
    const vec4 normal_map = texture(
        sampler2D(textures[nonuniformEXT(mesh.maps[NormalMap])], texture_sampler), texcoord);
    const vec3 tangent = Normalized(attr0.xyz);
    const vec3 normal = Normalized(attr4.xyz);
    const float handedness = float((0.0 < attr0.w ? 1 : 0) - (0.0 > attr0.w ? 1 : 0));
    const vec3 bitangent = vec3(tangent.y * -normal.z + normal.y * tangent.z,
                                tangent.z * -normal.x + normal.z * tangent.x,
                                tangent.x * -normal.y + normal.x * tangent.y) *
                           handedness;
    const float scale = pixel.constants[NormalScale];
    const vec3 bent = Normalized(vec3(scale * (2.0 * normal_map.x + -1.0),
                                      scale * (2.0 * normal_map.y + -1.0), normal_map.z));
    const vec3 surface = Normalized(bent.z * normal + (bent.y * bitangent + bent.x * tangent));

    // The highlight: a smooth step of the lit side, centred on the fresnel term less the normal
    // map's alpha past the border and as wide as the smooth times that alpha.
    const float fresnel = exp2(8.0 * log2(max(0.0, surface.z)));
    const float facing = pixel.constants[LightDirection + 2u] * surface.z +
                         (pixel.constants[LightDirection + 1u] * surface.y +
                          pixel.constants[LightDirection] * surface.x);
    const float lit = shadow * -facing;
    const float centre = normal_map.w * -(1.0 - pixel.constants[HighlightBorder]) +
                         (pixel.constants[HighlightFresnel] * fresnel + 1.0);
    const float low = -pixel.constants[HighlightSmooth] * normal_map.w + centre;
    const float high = pixel.constants[HighlightSmooth] * normal_map.w + centre;
    const float t = clamp((1.0 / (high - low)) * (0.5 + (0.5 * lit + -low)), 0.0, 1.0);
    const float highlight = -2.0 * (t * (t * t)) + 3.0 * (t * t);

    // The reflection map at the texture coordinates through ReflectionTecoord, moved by the
    // surface's x and y.
    const vec2 reflection_place =
        vec2(pixel.constants[ReflectionDistortion] * surface.x +
                 (pixel.constants[ReflectionTexcoord + 2u] * texcoord.x +
                  pixel.constants[ReflectionTexcoord]),
             pixel.constants[ReflectionDistortion] * surface.y +
                 (pixel.constants[ReflectionTexcoord + 3u] * texcoord.y +
                  pixel.constants[ReflectionTexcoord + 1u]));
    const float light = max(0.2, lit);
    const vec3 reflection =
        pixel.constants[ReflectionIntensity] *
        texture(sampler2D(textures[nonuniformEXT(mesh.maps[ReflectionMap])], texture_sampler),
                reflection_place)
            .rgb;
    const float rim = pixel.constants[HighlightIntensity] * highlight;
    const vec4 color = texture(
        sampler2D(textures[nonuniformEXT(mesh.maps[ColorMap])], texture_sampler), texcoord);
    const float luminance = reflection.b * 0.114 + (reflection.g * 0.587 + 0.299 * reflection.r);
    const float red = (pixel.constants[LightColor] * light) *
                      (pixel.constants[LightColor] * rim +
                       (pixel.constants[ColorScale] * color.r + reflection.r));
    const float green = (pixel.constants[LightColor + 1u] * light) *
                        (pixel.constants[LightColor + 1u] * rim +
                         (pixel.constants[ColorScale + 1u] * color.g + reflection.g));
    const float blue = (pixel.constants[LightColor + 2u] * light) *
                       (pixel.constants[LightColor + 2u] * rim +
                        (color.b * pixel.constants[ColorScale + 2u] + reflection.b));
    const float alpha =
        min(1.0, pixel.constants[LightColor + 3u] * rim +
                     (light * luminance + pixel.constants[ColorScale + 3u] * color.a));
    color0 = vec4(unpackHalf2x16(packHalf2x16(vec2(red, green))),
                  unpackHalf2x16(packHalf2x16(vec2(blue, alpha))));
}
