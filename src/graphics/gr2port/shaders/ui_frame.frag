// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's UI program 0x1b8019c: the colour with the first texture's red as coverage. With
// constants 4 and 5 not both 0, an outline of colour constants 0-2 and strength constant 3 grows
// around the coverage: the most covered of eight neighbours, constants 4 and 5 away along x and y.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "ui_pixel.glsl"

layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 0) out vec4 color0;

float Coverage(uvec2 texture_sampler, vec2 position) {
    return SampleTop(texture_sampler, position).x;
}

void main() {
    UiDraw draw = Draw();
    UiConstants constants = PixelConstants(draw);
    const uvec2 first = FirstTexture(draw);
    const float reach_x = constants.constants[4];
    const float reach_y = constants.constants[5];
    const float x = attr1.x;
    const float y = attr1.y;
    vec3 color = attr0.rgb;
    float coverage;
    if (reach_y == 0.0 && reach_x == 0.0) {
        coverage = Coverage(first, attr1.xy);
    } else {
        const float diagonal = uintBitsToFloat(0x3f333333u);
        const float left = diagonal * -reach_x + x;
        const float right = reach_x * diagonal + x;
        const float low = diagonal * -reach_y + y;
        const float high = reach_y * diagonal + y;
        const float low_left = Coverage(first, vec2(left, low));
        const float low_right = Coverage(first, vec2(right, low));
        const float below = Coverage(first, vec2(x, y - reach_y));
        const float centre = Coverage(first, vec2(x, y));
        const float before = Coverage(first, vec2(x - reach_x, y));
        const float high_left = Coverage(first, vec2(left, high));
        const float after = Coverage(first, vec2(reach_x + x, y));
        const float above = Coverage(first, vec2(x, reach_y + y));
        const float high_right = Coverage(first, vec2(right, high));
        const float strength = constants.constants[3];
        float most = max(low_left, low_right);
        const float hidden = -strength * centre;
        most = max(max(before, most), below);
        most = max(max(high_left, most), after);
        most = max(max(above, most), high_right);
        const float outline = min(1.0, most);
        const float total = outline * hidden + (strength * outline + centre);
        if (0.0 == total) {
            discard;
        }
        const float inverse = 1.0 / total;
        const float outside = -centre * inverse + 1.0;
        const float inside = centre * inverse;
        color = vec3(attr0.r * inside + constants.constants[0] * outside,
                     attr0.g * inside + constants.constants[1] * outside,
                     attr0.b * inside + constants.constants[2] * outside);
        coverage = min(1.0, total);
    }
    color0 = Export(Premultiply(color, attr0.w * coverage));
}
