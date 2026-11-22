// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle pixel program 0x1bef6a0, a mesh particle's surfaces: dithered by its alpha,
// punched through by its colour map's alpha, the colour map times ColorScale and the vertex colour
// halfway to its overlay with the layer values, the normal packed for the G-buffer, and the
// surface values times the mask.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "dither.glsl"
#include "particle_surface.glsl"

// The normal map's texture coordinates; the vertex colour; the normal, and the red layer value in
// w; the green and blue layer values and the alpha.
layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 2) in vec4 attr2;
layout(location = 3) in vec4 attr3;
layout(location = 0) out vec4 color0;
layout(location = 1) out vec4 color1;
layout(location = 2) out vec4 color2;

// The mesh's texture coordinates: the normal map's turned back through its offset and repeat.
vec2 MeshPlace(ParticleMaterial material, vec2 normal_place) {
    const vec2 offset = vec2(material.pixel_constants[NormalTransform],
                             material.pixel_constants[NormalTransform + 1u]);
    const vec2 repeat = vec2(material.pixel_constants[NormalTransform + 2u],
                             material.pixel_constants[NormalTransform + 3u]);
    const vec2 inverse = vec2(1.0 / repeat.x, 1.0 / repeat.y);
    return vec2(-offset.x * inverse.x + inverse.x * (repeat.x * normal_place.x + offset.x),
                -offset.y * inverse.y + inverse.y * (repeat.y * normal_place.y + offset.y));
}

void main() {
    ParticleDraw draw = Draw();
    // The pixel stays where its alpha reaches the game's dither noise.
    if (!(DitherNoise(GamePosition(draw), 1.0 - gl_FragCoord.z) > attr3.z)) {
        ParticleMaterial material = Material();
        const vec2 mesh_place = MeshPlace(material, attr0.xy);
        const vec4 color =
            SampleMap(material, ColorMap, MapPlace(material, ColorTransform, mesh_place));
        if (!(material.pixel_constants[PunchThrough] > color.a)) {
            const vec4 mask =
                SampleMap(material, MaskMap, MapPlace(material, MaskTransform, mesh_place));
            // Back faces turn the normal by its sign bits.
            const uvec3 turn = uvec3(gl_FrontFacing ? 0u : 0x80000000u);
            const vec2 packed_normal =
                PackNormal(uintBitsToFloat(floatBitsToUint(attr2.xyz) ^ turn));
            const float red = HalfOverlay(material.pixel_constants[ColorScale] * color.r, attr1.x,
                                          attr2.w);
            const float green =
                HalfOverlay(material.pixel_constants[ColorScale + 1u], attr1.y * color.g, attr3.x);
            const float blue =
                HalfOverlay(material.pixel_constants[ColorScale + 2u], attr1.z * color.b, attr3.y);
            const vec4 values = vec4(material.pixel_constants[Values],
                                     material.pixel_constants[Values + 1u],
                                     material.pixel_constants[Values + 2u],
                                     material.pixel_constants[Values + 3u]) *
                                mask;

            color0 = vec4(unpackHalf2x16(packHalf2x16(vec2(red, green))),
                          unpackHalf2x16(packHalf2x16(vec2(blue, 1.0))));
            color1 = vec4(unpackHalf2x16(packHalf2x16(packed_normal)), 0.0, 0.0);
            color2 = vec4(unpackHalf2x16(packHalf2x16(values.xy)),
                          unpackHalf2x16(packHalf2x16(values.zw)));
            return;
        }
    }
    discard;
}
