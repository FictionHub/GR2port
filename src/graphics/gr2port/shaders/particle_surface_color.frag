// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle pixel program 0x1bdb048, a mesh particle's surfaces with a layer: as
// particle_surface.frag, with each colour the colour map times ColorScale mixed over the layered
// colour map times its scale before the half overlay, and the surface values times the mask mixed
// over the layer's, both by the vertex colour's w.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "dither.glsl"
#include "particle_surface_layer.glsl"

// The normal map's texture coordinates; the vertex colour, and the base's weight over the layer
// in w; the normal, and the red layer value in w; the green and blue layer values and the alpha.
layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 2) in vec4 attr2;
layout(location = 3) in vec4 attr3;
layout(location = 0) out vec4 color0;
layout(location = 1) out vec4 color1;
layout(location = 2) out vec4 color2;

void main() {
    ParticleDraw draw = Draw();
    // The pixel stays where its alpha reaches the game's dither noise.
    if (!(DitherNoise(GamePosition(draw), 1.0 - gl_FragCoord.z) > attr3.z)) {
        ParticleMaterial material = Material();
        const vec2 mesh_place = MeshPlace(material, attr0.xy);
        const vec4 color =
            SampleMap(material, ColorMap, MapPlace(material, ColorTransform, mesh_place));
        if (!(material.pixel_constants[PunchThrough] > color.a)) {
            const float weight = attr1.w;
            const vec4 mask =
                SampleMap(material, MaskMap, MapPlace(material, MaskTransform, mesh_place));
            // Back faces turn the normal by its sign bits.
            const uvec3 turn = uvec3(gl_FrontFacing ? 0u : 0x80000000u);
            const vec2 packed_normal =
                PackNormal(uintBitsToFloat(floatBitsToUint(attr2.xyz) ^ turn));
            const vec4 layer = SampleMap(material, LayeredColorMap,
                                         MapPlace(material, LayeredColorTransform, mesh_place));
            const vec4 values = MixValues(vec4(material.pixel_constants[Values],
                                               material.pixel_constants[Values + 1u],
                                               material.pixel_constants[Values + 2u],
                                               material.pixel_constants[Values + 3u]) *
                                              mask,
                                          vec4(material.pixel_constants[LayeredValues],
                                               material.pixel_constants[LayeredValues + 1u],
                                               material.pixel_constants[LayeredValues + 2u],
                                               material.pixel_constants[LayeredValues + 3u]),
                                          weight);
            const float red = HalfOverlay(
                MixColor(material.pixel_constants[ColorScale] * color.r,
                         material.pixel_constants[LayeredColorScale] * layer.r, weight),
                attr1.x, attr2.w);
            const float green = HalfOverlay(
                MixColor(material.pixel_constants[ColorScale + 1u] * color.g,
                         material.pixel_constants[LayeredColorScale + 1u] * layer.g, weight),
                attr1.y, attr3.x);
            const float blue = HalfOverlay(
                MixColor(material.pixel_constants[ColorScale + 2u] * color.b,
                         material.pixel_constants[LayeredColorScale + 2u] * layer.b, weight),
                attr1.z, attr3.y);

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
