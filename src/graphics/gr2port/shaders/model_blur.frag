// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle pixel program 0x1be6040, a model blur: ten taps of the scene's colour along
// the blur's direction, red, green and blue each stepping its own length, then the blur end in
// particle_blur.glsl.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "particle_blur.glsl"

// The steps of red, green and blue and the particle's alpha; its distance in x; a vector whose
// length can pick the place on its curve in z and w; a value in thousandths that can pick it in z;
// the blur's direction in x and y; the particle's curve in x, as bits.
layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 2) in vec4 attr2;
layout(location = 3) in vec4 attr3;
layout(location = 6) in vec4 attr4;
layout(location = 10) flat in vec4 attr5;
layout(location = 0) out vec4 color0;

// The scene's colour a step along the direction, the direction's y turned for the texture's.
vec4 Tap(vec2 position, vec2 direction, float step, vec2 inverse_size) {
    return textureLod(sampler2D(background_texture, curve_sampler),
                      vec2(inverse_size.x * (direction.x * step + position.x),
                           inverse_size.y * (-direction.y * step + position.y)),
                      0.0);
}

void main() {
    ParticleDraw draw = Draw();
    const vec2 position = GamePosition(draw);
    const vec2 inverse_size =
        vec2(1.0 / draw.constants[ScreenSize], 1.0 / draw.constants[ScreenSize + 1u]);
    const vec3 steps = attr0.xyz;
    const vec2 direction = attr4.xy;
    // The counter moves on before the taps, as in the game's program, and the loop stays rolled.
    vec3 sum = vec3(0.0);
    uint tap = 0u;
    do {
        const float t = float(tap);
        tap++;
        sum = vec3(Tap(position, direction, steps.x * t, inverse_size).r,
                   Tap(position, direction, steps.y * t, inverse_size).g,
                   Tap(position, direction, steps.z * t, inverse_size).b) +
              sum;
    } while (tap < 10u);
    const float fade = DepthFade(draw, position * inverse_size, attr1.x);
    color0 = BlurredColor(draw, sum, attr0.w, fade, attr2.zw, attr3.z, floatBitsToUint(attr5.x));
}
