// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#version 460

layout(set = 0, binding = 0) uniform texture2D color_texture;
layout(set = 0, binding = 4) uniform sampler point_sampler;

layout(location = 0) out vec4 color0;

// Motion blur Low: the reconstruction's tiles back into the scene.
void main() {
    color0 = texelFetch(sampler2D(color_texture, point_sampler), ivec2(gl_FragCoord.xy), 0);
}
