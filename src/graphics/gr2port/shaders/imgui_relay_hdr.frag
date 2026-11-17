// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

#include "hdr.glsl"

layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler texture_sampler;

layout(push_constant) uniform Constants {
    layout(offset = 24) uint texture_handle;
    // The paper white in units of 10000 nits.
    float white;
};

layout(location = 0) in vec2 texcoord;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 frag_color;

// imgui_relay.frag's colour at the paper white of the HDR window, which blends it by alpha.
void main() {
    const vec4 c = color * texture(sampler2D(textures[texture_handle], texture_sampler), texcoord);
    frag_color = vec4(Hdr10(c.rgb, 1.0, white), c.a);
}
