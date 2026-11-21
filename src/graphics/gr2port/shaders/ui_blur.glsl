// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The blur of the game's UI programs 0x1b84544 and 0x1b84c34: 32 reads of the first texture along
// the step in constants 18 and 19. Weight i, constant i, takes the pair i steps back from the
// position and 15 - i steps back from the position moved by constants 16 and 17.

#extension GL_EXT_control_flow_attributes : require

#include "ui_pixel.glsl"

// The place k steps from the position, in the form the game's program computes each.
vec2 Tap(vec2 position, vec2 step, float k) {
    if (k == 0.0) {
        return position;
    }
    if (k == -1.0) {
        return position - step;
    }
    return k * step + position;
}

vec4 Blur(UiDraw draw, UiConstants constants, vec2 position) {
    const uvec2 first = FirstTexture(draw);
    const vec2 offset = vec2(constants.constants[16], constants.constants[17]);
    const vec2 step = vec2(constants.constants[18], constants.constants[19]);
    vec4 sum = constants.constants[0] * (Sample(first, offset + Tap(position, step, -15.0)) +
                                         Sample(first, Tap(position, step, 0.0)));
    [[unroll]] for (int i = 1; i < 16; i++) {
        const vec4 pair = Sample(first, offset + Tap(position, step, float(i - 15))) +
                          Sample(first, Tap(position, step, float(-i)));
        sum = constants.constants[i] * pair + sum;
    }
    return sum;
}
