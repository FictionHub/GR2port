// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle pixel program 0x1bdbbd4, a mesh particle's surfaces with a tangent and a
// layer: as particle_surface_tangent.frag, with the parallax, each colour, the normal maps'
// normals and the surface values mixed over the layer's by the vertex colour's w, as in
// particle_surface_color.frag.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "dither.glsl"
#include "particle_surface_layer.glsl"
#include "particle_surface_tangent.glsl"

// The tangent, and its handedness in w; the mesh's texture coordinates, and the parallax view in
// z and w; the vertex colour, and the base's weight over the layer in w; the normal, and the red
// layer value in w; the green and blue layer values and the alpha.
layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 2) in vec4 attr2;
layout(location = 3) in vec4 attr3;
layout(location = 4) in vec4 attr4;
layout(location = 0) out vec4 color0;
layout(location = 1) out vec4 color1;
layout(location = 2) out vec4 color2;

// The layer's normal map's offset and repeat, u_fLayeredParallax, and the layered normal map.
const uint LayeredNormalTransform = 20u;
const uint LayeredParallax = 38u;
const uint LayeredNormalMap = 4u;

// The game has flat normals where the layered normal map's handle is 0.
vec4 SampleLayeredNormalMap(ParticleMaterial material, vec2 place) {
    const uint handle = material.maps[LayeredNormalMap];
    return handle == 0u
               ? vec4(0.50196, 0.50196, 1.0, 1.0)
               : texture(sampler2D(textures[nonuniformEXT(handle)], texture_sampler), place);
}

// The normal maps' normals, stored from 0 to 1, mixed by the weight in the game's program's
// order.
vec3 MixNormal(vec3 base_map, vec3 layer_map, float weight) {
    const vec3 layer = 2.0 * layer_map + -1.0;
    return ((2.0 * base_map + -layer) + -1.0) * weight + layer;
}

void main() {
    ParticleDraw draw = Draw();
    // The pixel stays where its alpha reaches the game's dither noise.
    if (!(DitherNoise(GamePosition(draw), 1.0 - gl_FragCoord.z) > attr4.z)) {
        ParticleMaterial material = Material();
        const float weight = attr2.w;
        // Back faces turn the normal by its sign bits.
        const uvec3 turn = uvec3(gl_FrontFacing ? 0u : 0x80000000u);
        const vec3 normal = Normalized(uintBitsToFloat(floatBitsToUint(attr3.xyz) ^ turn));
        const float parallax = (material.pixel_constants[LayeredParallax] -
                                material.pixel_constants[Parallax]) *
                                   weight +
                               material.pixel_constants[Parallax];
        const vec2 place = ParallaxPlace(material, NormalPlace(material, attr1.xy),
                                         attr1.zw * parallax, normal.z);
        const vec2 mesh_place = MeshPlaceAt(material, place);
        const vec4 color =
            SampleMap(material, ColorMap, MapPlace(material, ColorTransform, mesh_place));
        if (!(material.pixel_constants[PunchThrough] > color.a)) {
            const vec4 mask =
                SampleMap(material, MaskMap, MapPlace(material, MaskTransform, mesh_place));
            const vec3 layer_normal_map =
                SampleLayeredNormalMap(material,
                                       MapPlace(material, LayeredNormalTransform, mesh_place))
                    .xyz;
            const vec3 normal_map =
                SampleNormalMap(material, vec2(LowBitCleared(place.x), place.y)).xyz;
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
            const vec2 packed_normal = PackNormal(FrameNormal(
                Normalized(MixNormal(normal_map, layer_normal_map, weight)), attr0, normal));
            const float red = FusedHalfOverlay(
                MixColor(material.pixel_constants[ColorScale] * color.r,
                         material.pixel_constants[LayeredColorScale] * layer.r, weight),
                attr2.x, attr3.w);
            const float green = FusedHalfOverlay(
                MixColor(material.pixel_constants[ColorScale + 1u] * color.g,
                         material.pixel_constants[LayeredColorScale + 1u] * layer.g, weight),
                attr2.y, attr4.x);
            const float blue = FusedHalfOverlay(
                MixColor(material.pixel_constants[ColorScale + 2u] * color.b,
                         material.pixel_constants[LayeredColorScale + 2u] * layer.b, weight),
                attr2.z, attr4.y);

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
