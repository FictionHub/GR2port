// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle pixel program 0x1bd97b0, a gem's surfaces: dithered by its alpha, its normal
// map turned by its tangent frame, its colour map seen along the view refracted by that normal,
// and the material's surface values. The program clears the lowest bit of the x of each sample.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "dither.glsl"
#include "particle_material.glsl"

// The tangent and its sign; the normal and the view's x; the view's y and z, and the texture
// coordinates; the alpha in x.
layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 2) in vec4 attr2;
layout(location = 3) in vec4 attr3;
layout(location = 0) out vec4 color0;
layout(location = 1) out vec4 color1;
layout(location = 2) out vec4 color2;

// The gem's pixel constants (FUN_013fc280): ColorScale, Color, Proj, Offset and Scale, the normal
// map's strength, the surface values, and the ratio of air's refractive index to the gem's.
const uint ColorScale = 0u;
const uint Color = 4u;
const uint Proj = 8u;
const uint Offset = 10u;
const uint Scale = 12u;
const uint NormalStrength = 14u;
const uint Surface = 15u;
const uint IndexRatio = 19u;

float LowBitCleared(float x) {
    return uintBitsToFloat(floatBitsToUint(x) & ~1u);
}

void main() {
    ParticleDraw draw = Draw();
    // The pixel stays where its alpha reaches the game's dither noise.
    if (!(DitherNoise(GamePosition(draw), 1.0 - gl_FragCoord.z) > attr3.x)) {
        ParticleMaterial material = Material();
        const float handedness = float((0.0 < attr0.w ? 1 : 0) - (0.0 > attr0.w ? 1 : 0));
        const vec3 tangent = Normalized(attr0.xyz);
        const vec3 normal = Normalized(attr1.xyz);
        // The normal crossed with the tangent, times the tangent's sign.
        const vec3 bitangent = vec3(tangent.y * -normal.z + normal.y * tangent.z,
                                    tangent.z * -normal.x + normal.z * tangent.x,
                                    tangent.x * -normal.y + normal.x * tangent.y) *
                               handedness;
        const vec3 view = Normalized(vec3(attr1.w, attr2.xy));
        const vec4 mapped = SampleNormalMap(material, vec2(LowBitCleared(attr2.z), attr2.w));
        const float strength = material.pixel_constants[NormalStrength];
        const vec3 bent = Normalized(vec3(strength * (2.0 * mapped.x + -1.0),
                                          strength * (2.0 * mapped.y + -1.0), mapped.z));
        const vec3 world = Normalized(bent.z * normal + (bent.y * bitangent + bent.x * tangent));

        // The view refracted by the normal, projected through Proj, Offset and Scale onto the
        // colour map. Where the view reflects whole, the place is NaN.
        const float ratio = material.pixel_constants[IndexRatio];
        const float cosine = world.z * view.z + (world.y * view.y + view.x * world.x);
        const float ratio2 = ratio * ratio;
        const float sine2 = ratio2 * -(cosine * cosine) + ratio2;
        const float along = ratio * -cosine + -sqrt(abs(1.0 - sine2));
        const vec3 refracted = world * along + ratio * view;
        const bool refracts = 1.0 > sine2;
        const float inverse_depth = 1.0 / ((refracts ? -1.0 : 0.0) * refracted.z);
        const float refraction = refracts ? 1.0 : 0.0;
        const vec2 proj = vec2(material.pixel_constants[Proj], material.pixel_constants[Proj + 1u]);
        const vec2 offset =
            vec2(material.pixel_constants[Offset], material.pixel_constants[Offset + 1u]);
        const vec2 scale =
            vec2(material.pixel_constants[Scale], material.pixel_constants[Scale + 1u]);
        const float x = (proj.x * inverse_depth * refraction * refracted.x + offset.x) * 0.5;
        const float y = proj.y * (inverse_depth * refraction) * refracted.y + offset.y;
        const vec2 place = vec2(LowBitCleared(scale.x * x + 0.5), -0.5 * (scale.y * y) + 0.5);
        const vec4 texel = SampleMap(material, ColorMap, place);

        // The normal's x and y over their length, times sqrt(0.5 - 0.5 z).
        const float xy2 = max(uintBitsToFloat(0x358637bdu), world.y * world.y + world.x * world.x);
        const float normal_scale = 1.0 / sqrt(xy2) * sqrt(abs(-0.5 * world.z + 0.5));
        const vec2 packed_normal = vec2(normal_scale * world.x, normal_scale * world.y);

        // ColorScale times the colour map, over Color by the map's alpha.
        const vec3 color_scale =
            vec3(material.pixel_constants[ColorScale], material.pixel_constants[ColorScale + 1u],
                 material.pixel_constants[ColorScale + 2u]);
        const vec3 color = vec3(material.pixel_constants[Color],
                                material.pixel_constants[Color + 1u],
                                material.pixel_constants[Color + 2u]);
        const vec3 albedo = -color * texel.a + (color_scale * texel.rgb * texel.a + color);

        color0 = vec4(unpackHalf2x16(packHalf2x16(albedo.rg)),
                      unpackHalf2x16(packHalf2x16(vec2(albedo.b, 1.0))));
        color1 = vec4(unpackHalf2x16(packHalf2x16(packed_normal)), 0.0, 0.0);
        color2 = vec4(unpackHalf2x16(packHalf2x16(vec2(material.pixel_constants[Surface],
                                                       material.pixel_constants[Surface + 1u]))),
                      unpackHalf2x16(packHalf2x16(vec2(material.pixel_constants[Surface + 2u],
                                                       material.pixel_constants[Surface + 3u]))));
        return;
    }
    discard;
}
