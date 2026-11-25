// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's Composite program, for a shader that declares grade.

// The game's Composite program with the lens and the wobble on.
vec2 Distort(vec2 texcoord) {
    const vec2 center = texcoord - 0.5;
    const float bulge =
        1.0 / (grade.distortion.y * sqrt(1.0 - grade.distortion.z * dot(center, center)));
    const vec2 lens = center * (grade.distortion.x * (bulge - 1.0) + 1.0) + 0.5;
    const float phase = grade.fluctuation.x;
    const vec2 fine = sin(vec2(157.0, 314.0) * lens + phase);
    const vec2 coarse = cos(vec2(31.4, 62.8) * lens + phase);
    const vec2 from_center = 2.0 * lens - 1.0;
    const float edge = 1.0 - dot(from_center, from_center);
    return lens + (0.002 * coarse + 0.001 * fine) * grade.fluctuation.y * (1.0 - edge * edge);
}

// Knot i of a channel. A knot before the first reads 0, as the game's buffer load there gives.
float Knot(uint channel, int i) {
    return i >= 0 ? grade.knots[channel][max(i, 0)] : 0.0;
}

// A channel's Catmull-Rom curve through its knots, with the end knots repeated.
float Curve(uint channel, float value) {
    const float scaled = value * 6.0;
    const int segment = min(int(scaled), 5);
    const float x = scaled - float(segment);
    const float p0 = grade.knots[channel][max(segment - 1, 0)];
    const float p1 = Knot(channel, segment);
    const float p2 = Knot(channel, segment + 1);
    const float p3 = Knot(channel, min(segment + 2, 6));
    const float a = p0 - 2.5 * p1 + 2.0 * p2 - 0.5 * p3;
    const float b = 1.5 * (p1 - p2) + 0.5 * (p3 - p0);
    return p1 + x * (0.5 * (p2 - p0) + x * (a + x * b));
}

// The game's Composite program: saturation in YCbCr, each channel's curve, then the sRGB encoding,
// as the target is UNORM.
vec3 Composite(vec3 c) {
    const float y = dot(c, vec3(0.299, 0.587, 0.114));
    const float cb = dot(c, vec3(-0.16874, -0.33126, 0.5)) * grade.saturation;
    const float cr = dot(c, vec3(0.5, -0.41869, -0.08131)) * grade.saturation;
    const vec3 saturated = clamp(y + vec3(1.402 * cr, -0.34414 * cb - 0.71414 * cr, 1.772 * cb),
                                 vec3(0.0), vec3(1.0));
    const vec3 curved =
        max(vec3(Curve(0u, saturated.r), Curve(1u, saturated.g), Curve(2u, saturated.b)),
            vec3(0.0));
    return mix(1.055 * pow(curved, vec3(1.0 / 2.4)) - 0.055, 12.92 * curved,
               lessThan(curved, vec3(0.0031308)));
}
