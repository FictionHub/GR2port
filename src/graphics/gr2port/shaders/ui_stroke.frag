// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's UI program 0x1b765d0, the comic panels' ink strokes: the colour with an alpha that
// makes a wobbling line of width constant 0 from the stroke geometry program's distances in
// location 1. Sums of cosines along the stroke, whose phase is constant 1 times x plus w, move the
// line's sides; smooth steps round its ends and sides, and constant 2 sets how sharp it is.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "ui_pixel.glsl"

layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 0) out vec4 color0;

float K(uint bits) {
    return uintBitsToFloat(bits);
}

// Smooth step's curve, 3t^2 - 2t^3.
float Curve(float t) {
    const float squared = t * t;
    const float cubed = t * squared;
    return -2.0 * cubed + 3.0 * squared;
}

float Wave(float turns) {
    return cos(6.283185307 * fract(abs(turns)));
}

void main() {
    UiConstants constants = PixelConstants(Draw());
    const float width = constants.constants[0];
    const float along = constants.constants[1] * attr1.x + attr1.w;

    // The two wobbles: sums of three cosines each.
    const float wobble_a = Wave(fma(along, K(0x3ad09b89u), K(0x3e5f4679u))) +
                           Wave(fma(fma(along, K(0x3d088889u), K(0x40922223u)), K(0x3e22f983u),
                                    K(0x3db8b462u)));
    const float turn_a = fma(fma(along, K(0x3c94f209u), K(0x401f6b0eu)), K(0x3e22f983u),
                             K(0x3d6d0dd5u));
    const float turn_b = fma(fma(along, K(0x3d088889u), K(0x411f7778u)), K(0x3e22f983u),
                             K(0x3db8b462u));
    const float turn_c = fma(along, K(0x3ad09b89u), K(0x3ef3a5a9u));
    const float turn_d = fma(fma(along, K(0x3c94f209u), K(0x40adf6b1u)), K(0x3e22f983u),
                             K(0x3d6d0dd5u));
    const float half_width = 0.5 * width;
    const float wobble_b = (Wave(turn_c) + Wave(turn_b)) + Wave(turn_d);
    const float sum_a = wobble_a + Wave(turn_a);
    const float inverse_half = 1.0 / half_width;
    const float shift_b = (wobble_b * wobble_b) * half_width;
    const float shift_a = (sum_a * sum_a) * half_width;

    // The ends.
    const float start = Curve(clamp(attr1.x * inverse_half, 0.0, 1.0));
    const float to_end = attr1.z - attr1.x;
    const float end = Curve(clamp(to_end * inverse_half, 0.0, 1.0));
    const float end_offset = 0.5 * -width + to_end;
    const float inverse_span = 1.0 / (1.5 * width + -half_width);
    const float end_inside = Curve(clamp(end_offset * inverse_span, 0.0, 1.0));
    const float start_offset = 0.5 * -width + attr1.x;
    const float start_inside = Curve(clamp(start_offset * inverse_span, 0.0, 1.0));
    const float inside = start_inside * end_inside;
    const float side_b = shift_b * inside;
    const float side_a = shift_a * inside;
    const float power = (start_inside * end_inside + -1.0) * constants.constants[2] + 6.0;

    // The sides.
    const float lift = K(0x3e4ccccdu) * (width * inside);
    const float reach_a = K(0x3ede9bd3u) * -side_a + lift;
    const float reach_b = K(0x3ede9bd3u) * -side_b + lift;
    const float across = attr1.y;
    const float below = clamp((across - reach_b) * (1.0 / (0.5 * width + -reach_b)), 0.0, 1.0);
    const float above = clamp(((width - across) - reach_a) * (1.0 / (0.5 * width + -reach_a)),
                              0.0, 1.0);
    const float across_offset = 0.5 * -width + across;
    const float across_squared = across_offset * across_offset;
    const float start_distance = sqrt(start_offset * start_offset + across_squared);
    const float end_distance = sqrt(end_offset * end_offset + across_squared);
    const float end_round = Curve(clamp(inverse_half * end_distance, 0.0, 1.0));
    const float start_round = Curve(clamp(start_distance * inverse_half, 0.0, 1.0));
    const float start_out = 1.0 - start_round;
    const float body = (Curve(above) * Curve(below) + -start_out) * start + start_out;
    const float end_out = 1.0 - end_round;
    const float shape = (body - end_out) * end + end_out;

    const float sharp = clamp(2.0 * exp2(log2(abs(shape)) * power) + -1.0, 0.0, 1.0);
    color0 = Export(Premultiply(attr0.rgb, attr0.w * Curve(sharp)));
}
