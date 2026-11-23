// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the game's GPU particle model programs read beside particle_gpu_material.glsl: the
// material's fade of the model by the angle its normal makes with the view. The combiner's
// programs define PARTICLE_COMBINER_EDGE (and one PARTICLE_COMBINER_EDGE_ROUNDED) for their
// rounding of it.

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
#ifdef PARTICLE_COMBINER_EDGE
        // The combiner's programs take the low mark off the base times the term before term +
        // base is added, and take the smooth step as 1 - t^2 (3 - 2t): one fused step, or with
        // PARTICLE_COMBINER_EDGE_ROUNDED the product rounded before it comes off 1.
        precise const float above = fma(edge.w, -term, -edge.y) + (term + edge.w);
        const float t = clamp((1.0 / (edge.z - edge.y)) * above, 0.0, 1.0);
        precise const float square = t * t;
#ifdef PARTICLE_COMBINER_EDGE_ROUNDED
        precise const float rest = 1.0 - square * (3.0 - 2.0 * t);
#else
        precise const float rest = fma(-square, 3.0 - 2.0 * t, 1.0);
#endif
        fade = rest;
#else
        // The base plus the rest of 1 times the term, one fused step on term + base; the low
        // mark comes off after it.
        const float fresnel = fma(edge.w, -term, term + edge.w);
        const float t = clamp((1.0 / (edge.z - edge.y)) * (fresnel - edge.y), 0.0, 1.0);
        fade = 1.0 - SmoothPolynomial(t);
#endif
    } else if (uintBitsToFloat(0x3f7ffffeu) < edge.x) {
        fade = SmoothPolynomial(clamp((abs(normal_z) - edge.y) * (1.0 / (edge.z - edge.y)), 0.0,
                                      1.0));
    }
    return fade;
}
