// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the game's mesh particle surface programs with a layer share beside particle_surface.glsl:
// the layer's constants and colour map, the mesh's place from the normal map's, and the base
// material mixed over the layer by a weight, 1 for the base.

#include "particle_surface.glsl"

// The layer's colour scale, its colour map's offset and repeat, and its surface values with its
// emissive (WriteSurfaceMaterial).
const uint LayeredColorScale = 4u;
const uint LayeredColorTransform = 12u;
const uint LayeredValues = 34u;
const uint LayeredColorMap = 3u;

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

// The game's programs mix a colour and the surface values in different orders of operations.
float MixColor(float base, float layer, float weight) {
    return (base + -layer) * weight + layer;
}

vec4 MixValues(vec4 base, vec4 layer, float weight) {
    return -layer * weight + (base * weight + layer);
}
