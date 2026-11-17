// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

#include "hdr.glsl"

layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 1, binding = 0) uniform sampler samplers[4];
// The world's colour as Composite drew it, and its gain.
layout(set = 2, binding = 0) uniform texture2D world;

layout(push_constant) uniform Constants {
    uint picture;
    // Whether world holds this frame's world.
    uint has_world;
    // The paper white in units of 10000 nits.
    float white;
};

layout(location = 0) in vec2 position;

layout(location = 0) out vec4 color;

// How much of the world's colour the screen drawing left: 1 where it kept it or darkened it
// evenly, as fades and dark panels do, and 0 where it drew over it.
float Kept(vec3 shown, vec3 drawn) {
    const float scale = min(dot(shown, drawn) / max(dot(drawn, drawn), 1e-6), 1.0);
    const vec3 off = abs(shown - scale * drawn);
    return 1.0 - smoothstep(1.5 / 255.0, 3.0 / 255.0, max(max(off.r, off.g), off.b));
}

// The frame's back buffer over the HDR window, through the clamping bilinear sampler. The world's
// highlights take their gain where the screen drawing left them.
void main() {
    const vec2 texcoord = 0.5 * position + 0.5;
    const vec3 shown = texture(sampler2D(textures[picture], samplers[1]), texcoord).rgb;
    float gain = 1.0;
    if (has_world != 0u) {
        const vec4 drawn = texture(sampler2D(world, samplers[1]), texcoord);
        gain = mix(1.0, drawn.a, Kept(shown, drawn.rgb));
    }
    color = vec4(Hdr10(shown, gain, white), 1.0);
}
