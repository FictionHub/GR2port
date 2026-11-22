// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the game's mesh particle surface programs share beside particle_material.glsl: the surface
// material's constants, the places of its maps, the colour's half overlay with the layer values,
// and the G-buffer's packed normal.

#ifndef PARTICLE_SURFACE_GLSL
#define PARTICLE_SURFACE_GLSL

#include "particle_material.glsl"

// The surface material's pixel constants (WriteSurfaceMaterial): ColorScale, the punch-through
// threshold, each map's offset and repeat, and the surface values with the emissive.
const uint ColorScale = 0u;
const uint PunchThrough = 3u;
const uint ColorTransform = 8u;
const uint NormalTransform = 16u;
const uint MaskTransform = 24u;
const uint Values = 29u;
const uint MaskMap = 2u;

float LowBitCleared(float x) {
    return uintBitsToFloat(floatBitsToUint(x) & ~1u);
}

// A map's place: its repeat times the mesh's coordinates, plus its offset. The programs clear the
// lowest bit of the x.
vec2 MapPlace(ParticleMaterial material, uint transform, vec2 mesh_place) {
    return vec2(LowBitCleared(material.pixel_constants[transform + 2u] * mesh_place.x +
                              material.pixel_constants[transform]),
                material.pixel_constants[transform + 3u] * mesh_place.y +
                    material.pixel_constants[transform + 1u]);
}

// A colour channel, factor times other, halfway to its overlay with the layer value, in the order
// of operations of the game's programs.
float HalfOverlay(float factor, float other, float layer) {
    const float base = factor * other;
    const bool bright = 0.5 <= base;
    const float flip = bright ? -1.0 : 0.0;
    const float product = layer * base;
    float overlay = 2.0 * product + (bright ? 1.0 : 0.0);
    overlay = 2.0 * product * flip + overlay;
    overlay = (-2.0 * ((1.0 - layer) * base) + fma(-layer, 2.0, 2.0)) * flip + overlay;
    const float difference = factor * -other + overlay;
    return 0.5 * difference + base;
}

// The G-buffer's normal: x and y over their length, times sqrt(0.5 z + 0.5) of the normal with z
// turned.
vec2 PackNormal(vec3 normal) {
    const float inverse =
        1.0 / sqrt(normal.z * normal.z + (normal.y * normal.y + normal.x * normal.x));
    const float x = inverse * normal.x;
    const float y = inverse * normal.y;
    const float z = -inverse * normal.z;
    const float xy2 = max(uintBitsToFloat(0x358637bdu), y * y + x * x);
    const float scale = 1.0 / sqrt(xy2) * sqrt(abs(0.5 * z + 0.5));
    return vec2(scale * x, scale * y);
}

#endif
