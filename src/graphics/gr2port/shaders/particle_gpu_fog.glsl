// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The fog of the game's GPU particle pixel programs for the fog passes, scaled by the particle's
// material. Include particle_gpu_material.glsl and particle_fog.glsl first.

// The material's scales of the fog (24 to 26) and the curve that replaces them where it is above
// 0.
const uint MaterialFog = 24u;
const uint MaterialFogCurve = 126u;

// The colour through the fog as Fogged, with the material's scales at x, each 1 below 2^-23: of
// the volumetric fog's texture (x), of the fog's colour (y) and of its strength (z).
vec3 MaterialFogged(uint material, uint curve_offset, float x, vec3 color, vec2 place,
                    vec3 position) {
    const vec4 scale = MaterialCurve(material, MaterialFogCurve, curve_offset, x,
                                     MaterialVector(material, MaterialFog));
    const float volume_scale = uintBitsToFloat(0x34000000u) > scale.x ? 1.0 : scale.x;
    const float color_scale = uintBitsToFloat(0x34000000u) > scale.y ? 1.0 : scale.y;
    const float strength_scale = uintBitsToFloat(0x34000000u) > scale.z ? 1.0 : scale.z;
    FogConstants fog = FogConstants(fog_address);
    const vec4 volume = FogThrough(place, position);
    const float through = volume.a;
    const vec3 kept = fma(vec3(volume_scale), volume.rgb, color * through);
    const float cover = (fog.words[7] * strength_scale) *
                        clamp(uintBitsToFloat(0xbf317218u) * log2(through), 0.0, 1.0);
    const vec3 fog_color = vec3(fog.words[4], fog.words[5], fog.words[6]);
    return fma(fma(fog_color, vec3(color_scale), -kept), vec3(cover), kept);
}
