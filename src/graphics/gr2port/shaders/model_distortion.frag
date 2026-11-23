// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle pixel program 0x1bea3fc, a distortion model: the scene's red, green and blue
// behind the particle, each moved its own distance along its distortion map's direction turned
// into the particle's frame, faded by the depth behind it and coloured by its curve, times its
// alpha.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "particle_frame.glsl"

// The strengths of red, green and blue and the particle's alpha; its distance in x; the map's
// texture coordinates in x and y, and a vector whose length can pick the place on its curve in z
// and w; a value in thousandths that can pick it in z; the frame's normal; the frame's tangent;
// the particle's curve in x, as bits.
layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 2) in vec4 attr2;
layout(location = 3) in vec4 attr3;
layout(location = 6) in vec4 attr4;
layout(location = 7) in vec4 attr5;
layout(location = 10) flat in vec4 attr6;
layout(location = 0) out vec4 color0;

void main() {
    ParticleDraw draw = Draw();
    const vec2 position = GamePosition(draw);
    const vec2 inverse_size =
        vec2(1.0 / draw.constants[ScreenSize], 1.0 / draw.constants[ScreenSize + 1u]);
    const vec3 color = FrameScene(position, DistortionMap(draw, attr2.xy), attr4.xyz, attr5.xyz,
                                  attr0.xyz, inverse_size);
    const float fade = DepthFade(draw, position * inverse_size, attr1.x);
    color0 = DistortedColor(draw, color, attr0.w, fade, attr2.zw, attr3.z,
                            floatBitsToUint(attr6.x));
}
