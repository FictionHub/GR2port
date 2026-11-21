// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's UI program 0x1b739b8: each channel of the first texture's top level through its
// curve, 12 points at 0, 1/11, ..., 1 joined by straight lines, in constants 0-11 for red, 12-23
// for green and 24-35 for blue.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "ui_pixel.glsl"

layout(location = 1) in vec4 attr0;
layout(location = 0) out vec4 color0;

float K(uint bits) {
    return uintBitsToFloat(bits);
}

// The line from point k to point k + 1 of the curve whose points start at constant first, at the
// place scale * x + offset takes x to across the segment.
float Segment(UiConstants constants, uint first, uint k, float scale, float offset, float x) {
    const float low = constants.constants[first + k];
    const float rise = constants.constants[first + k + 1u] - low;
    return rise * (scale * x + offset) + low;
}

// The game's program finds the segment with this search over the segments' starts, k / 11 as it
// holds them, and holds 11 for the last five scales and offsets slightly off.
float Curve(UiConstants constants, uint first, float x) {
    if (K(0x3f0ba2e9u) > x) {
        if (K(0x3e8ba2e9u) > x) {
            if (K(0x3e3a2e8cu) > x) {
                if (K(0x3dba2e8cu) > x) {
                    const float low = constants.constants[first];
                    const float rise = constants.constants[first + 1u] - low;
                    return 11.0 * (rise * x) + low;
                }
                return Segment(constants, first, 1u, 11.0, -1.0, x);
            }
            return Segment(constants, first, 2u, 11.0, -2.0, x);
        }
        if (K(0x3ee8ba2fu) > x) {
            if (K(0x3eba2e8cu) > x) {
                return Segment(constants, first, 3u, 11.0, -3.0, x);
            }
            return Segment(constants, first, 4u, 11.0, -4.0, x);
        }
        return Segment(constants, first, 5u, 11.0, -5.0, x);
    }
    if (K(0x3f3a2e8cu) > x) {
        if (K(0x3f22e8bau) > x) {
            return Segment(constants, first, 6u, K(0x41300003u), K(0xc0c00004u), x);
        }
        return Segment(constants, first, 7u, K(0x412ffffcu), K(0xc0dffffbu), x);
    }
    if (K(0x3f68ba2fu) > x) {
        if (K(0x3f51745eu) > x) {
            return Segment(constants, first, 8u, K(0x412ffffcu), K(0xc0fffffbu), x);
        }
        return Segment(constants, first, 9u, K(0x41300003u), K(0xc1100003u), x);
    }
    return Segment(constants, first, 10u, K(0x41300003u), K(0xc1200003u), x);
}

void main() {
    UiDraw draw = Draw();
    UiConstants constants = PixelConstants(draw);
    const vec3 color = clamp(max(vec3(0.0), SampleTop(FirstTexture(draw), attr0.xy).rgb), 0.0, 1.0);
    color0 = Export(vec4(Curve(constants, 0u, color.r), Curve(constants, 12u, color.g),
                         Curve(constants, 24u, color.b), 1.0));
}
