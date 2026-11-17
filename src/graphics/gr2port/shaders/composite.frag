// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#version 460
#extension GL_GOOGLE_include_directive : require

#include "scene.glsl"

layout(set = 0, binding = 0) uniform sampler2D picture;

layout(push_constant) uniform Constants {
    Grade grade;
    Meter meter;
};

layout(location = 0) in vec2 position;

layout(location = 0) out vec4 color;

#include "composite.glsl"

void main() {
    vec2 texcoord = 0.5 * position + 0.5;
    if (grade.distorted != 0u) {
        texcoord = Distort(texcoord);
    }
    color = vec4(Composite(texture(picture, texcoord).rgb), 1.0);
}
