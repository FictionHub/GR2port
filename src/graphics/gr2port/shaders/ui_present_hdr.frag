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

// The frame's back buffer over the HDR window, through the clamping bilinear sampler. With the
// world in it, the back buffer's alpha holds the share of the world the screen drawing left, and
// only that part of the colour takes the world's gain. A draw that multiplies the world leaves the
// alpha alone, so the colour it leaves also caps that part.
void main() {
    const vec2 texcoord = 0.5 * position + 0.5;
    const vec4 shown = texture(sampler2D(textures[picture], samplers[1]), texcoord);
    vec3 light = DecodeSrgb(shown.rgb);
    if (has_world != 0u) {
        const vec4 drawn = texture(sampler2D(world, samplers[1]), texcoord);
        const vec3 left = clamp(drawn.rgb * shown.a, vec3(0.0), shown.rgb);
        light += DecodeSrgb(left) * (drawn.a - 1.0);
    }
    color = vec4(Pq(Bt2020(light) * white), 1.0);
}
