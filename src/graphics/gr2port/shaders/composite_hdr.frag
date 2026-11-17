// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// Composite for an HDR window: the SDR picture into the back buffer, and beside it in the gain
// image that colour and the factor that takes its light to the HDR picture's.

#version 460
#extension GL_GOOGLE_include_directive : require

#include "scene.glsl"

layout(set = 0, binding = 0) uniform sampler2D picture;
// The tonemap's output before the picture clips it at 1.
layout(set = 0, binding = 1) uniform sampler2D toned;

layout(push_constant) uniform Constants {
    Grade grade;
    Meter meter;
    // The display's peak in paper whites, at least 1.
    float peak;
};

layout(location = 0) in vec2 position;

layout(location = 0) out vec4 color;
layout(location = 1) out vec4 gain;

#include "composite.glsl"

// Below the knee the HDR picture is the SDR one.
const float Knee = 0.8;

// The exposed scene brightness that tonemap.frag's curve tones to t. It grows without bound as t
// nears the curve's limit, 2.22913.
float Untone(float t) {
    const float r = min((t + 0.07687) / 2.3060, 0.9999);
    const float a = 0.22 * (1.0 - r);
    const float b = 0.03 - 0.3 * r;
    const float c = 0.002 - 0.06 * r;
    return (sqrt(max(b * b - 4.0 * a * c, 0.0)) - b) / (2.0 * a);
}

// The HDR curve: from the knee on, it rises from the tonemap's slope there towards the peak
// instead of clipping at 1.
float HdrTone(float x) {
    const float knee_x = Untone(Knee);
    const float n = (0.22 * knee_x + 0.03) * knee_x + 0.002;
    const float d = (0.22 * knee_x + 0.3) * knee_x + 0.06;
    const float slope = 2.3060 * ((0.44 * knee_x + 0.03) * d - n * (0.44 * knee_x + 0.3)) / (d * d);
    const float room = peak - Knee;
    return Knee + room * (1.0 - exp(-slope * (x - knee_x) / room));
}

// The factor from the picture's light to the HDR picture's, by the brightest channel. It fades in
// as the picture nears white, which keeps it off the edges of bright things, where the toned
// scene, unantialiased and at the render size, does not line up with the picture.
float Gain(vec3 toned_color, vec3 picture_color) {
    const float t = max(max(toned_color.r, toned_color.g), toned_color.b);
    if (t <= Knee) {
        return 1.0;
    }
    const float full = max(HdrTone(Untone(t)) / min(t, 1.0), 1.0);
    const float shown = max(max(picture_color.r, picture_color.g), picture_color.b);
    return mix(1.0, full, smoothstep(Knee, 1.0, shown));
}

void main() {
    vec2 texcoord = 0.5 * position + 0.5;
    if (grade.distorted != 0u) {
        texcoord = Distort(texcoord);
    }
    const vec3 picture_color = texture(picture, texcoord).rgb;
    color = vec4(Composite(picture_color), 1.0);
    gain = vec4(color.rgb, Gain(texture(toned, texcoord).rgb, picture_color));
}
