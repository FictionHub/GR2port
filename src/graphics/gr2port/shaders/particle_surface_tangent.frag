// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle pixel program 0x1bda2f4, a mesh particle's surfaces with a tangent: as
// particle_surface.frag, with the maps' place moved by parallax through the normal map's heights,
// and the G-buffer's normal the normal map's turned by the tangent frame.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "dither.glsl"
#include "particle_surface_tangent.glsl"

// The tangent, and its handedness in w; the mesh's texture coordinates, and the parallax view in
// z and w; the vertex colour; the normal, and the red layer value in w; the green and blue layer
// values and the alpha.
layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 2) in vec4 attr2;
layout(location = 3) in vec4 attr3;
layout(location = 4) in vec4 attr4;
layout(location = 0) out vec4 color0;
layout(location = 1) out vec4 color1;
layout(location = 2) out vec4 color2;

void main() {
    ParticleDraw draw = Draw();
    // The pixel stays where its alpha reaches the game's dither noise.
    if (!(DitherNoise(GamePosition(draw), 1.0 - gl_FragCoord.z) > attr4.z)) {
        ParticleMaterial material = Material();
        // Back faces turn the normal by its sign bits.
        const uvec3 turn = uvec3(gl_FrontFacing ? 0u : 0x80000000u);
        const vec3 normal = Normalized(uintBitsToFloat(floatBitsToUint(attr3.xyz) ^ turn));
        const vec2 place = ParallaxPlace(material, NormalPlace(material, attr1.xy),
                                         material.pixel_constants[Parallax] * attr1.zw, normal.z);
        const vec2 mesh_place = MeshPlaceAt(material, place);
        const vec4 color =
            SampleMap(material, ColorMap, MapPlace(material, ColorTransform, mesh_place));
        if (!(material.pixel_constants[PunchThrough] > color.a)) {
            const vec4 mask =
                SampleMap(material, MaskMap, MapPlace(material, MaskTransform, mesh_place));
            const vec3 map_normal =
                2.0 * SampleNormalMap(material, vec2(LowBitCleared(place.x), place.y)).xyz +
                -1.0;
            const vec2 packed_normal = PackNormal(FrameNormal(map_normal, attr0, normal));
            const float red = HalfOverlay(material.pixel_constants[ColorScale] * color.r, attr2.x,
                                          attr3.w);
            const float green =
                FusedHalfOverlay(material.pixel_constants[ColorScale + 1u], attr2.y * color.g,
                                 attr4.x);
            const float blue =
                FusedHalfOverlay(material.pixel_constants[ColorScale + 2u], attr2.z * color.b,
                                 attr4.y);
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
