// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The light of the game's lit GPU particle pixel programs, shifted by the particle's material.
// Include particle_gpu_material.glsl and particle_lit.glsl first.

// The material's shift of the lights' luma (28, 29) and the curve that replaces it where it is
// above 0.
const uint MaterialLightShift = 28u;
const uint MaterialLightShiftCurve = 127u;

// The material's shift of the lights at x (see NoLightShift): each of its two values 0 below
// 2^-23, else held to 0 to 2, less 1.
vec2 LightShift(uint material, uint curve_offset, float x) {
    const vec4 shift = MaterialCurve(material, MaterialLightShiftCurve, curve_offset, x,
                                     MaterialVector(material, MaterialLightShift));
    return vec2(uintBitsToFloat(0x34000000u) > shift.x ? 0.0 : -1.0 + clamp(shift.x, 0.0, 2.0),
                uintBitsToFloat(0x34000000u) > shift.y ? 0.0 : -1.0 + clamp(shift.y, 0.0, 2.0));
}
