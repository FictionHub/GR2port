// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the game's GPU particle model programs read beside particle_gpu_material.glsl: the
// material's fade of the model by the angle its normal makes with the view.

// The material's edge (words 12 to 15): its mode, the low and high marks of its smooth step and
// the base of its Fresnel term; the curve that replaces it where it is above 0.
const uint MaterialEdge = 12u;
const uint MaterialEdgeCurve = 123u;

// The smooth step's polynomial at t, 3t^2 - 2t^3.
float SmoothPolynomial(float t) {
    const float square = t * t;
    return fma(-2.0, t * square, 3.0 * square);
}

// How much of the model shows where its normal's z faces the view: from mode 2, less as a Fresnel
// term of 1 - z with the edge's base rises from the low mark to the high; from 1 - 2^-24, more as
// |z| does; elsewhere all of it.
float EdgeFade(uint material, uint curve_offset, float x, float normal_z) {
    const vec4 edge = MaterialCurve(material, MaterialEdgeCurve, curve_offset, x,
                                    MaterialVector(material, MaterialEdge));
    float fade = 1.0;
    if (uintBitsToFloat(0x3fffffffu) < edge.x) {
        const float term = exp2(5.0 * log2(abs(1.0 - normal_z)));
        // The base plus the rest of 1 times the term, one fused step on term + base; the low
        // mark comes off after it.
        const float fresnel = fma(edge.w, -term, term + edge.w);
        const float t = clamp((1.0 / (edge.z - edge.y)) * (fresnel - edge.y), 0.0, 1.0);
        fade = 1.0 - SmoothPolynomial(t);
    } else if (uintBitsToFloat(0x3f7ffffeu) < edge.x) {
        fade = SmoothPolynomial(clamp((abs(normal_z) - edge.y) * (1.0 / (edge.z - edge.y)), 0.0,
                                      1.0));
    }
    return fade;
}
