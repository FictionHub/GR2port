// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's ShadowTrace pixel program 0x1bbd8dc, the point lights' screen-space shadow: for each
// light of the pixel's cell in LightCompute's grid that has a shadow share, ten jittered steps
// from the pixel's view position toward the light, each weighed by how far the scene's depth lies
// behind it; the steps' share, times 1 less the light's falloff, summed over the lights to at most
// 1, in red and green.
#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(set = 0, binding = 0) uniform sampler2D scene_depth;
layout(push_constant) uniform Push {
    uint64_t constants_address;
    uint64_t point_lights_address;
    uint64_t light_grid_address;
};
layout(location = 0) out vec4 color0;

// Scene::ShadowTrace: u_m4Proj, u_f4InverseView, u_f4TargetSize (the size, then its inverse),
// u_uLight, u_fShadowRadius, u_fShadowDensity and u_fShadowJitter.
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer TraceConstants {
    float projection[16];
    float inverse_view[4];
    float target_size[4];
    uint lights;
    float radius;
    float density;
    float jitter;
};
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer TraceWords {
    uint words[];
};

// The view's point lights, 48 bytes each (Scene::PointLight), and LightCompute's grid of 60 x 36
// x 8 cells, each a list of 48 light indices that 0xffffffff ends. Reads outside them give 0 and
// 0xffffffff.
const uint PointLightWords = 1024u * 12u;
const uint LightGridWords = 60u * 36u * 8u * 48u;
const uint LightRange = 12u;
const uint LightFalloffStart = 28u;
const uint LightFalloffPower = 32u;
const uint LightShadow = 36u;

// A word of a light at a byte offset, addressed in 32 bits as the game's program does.
uint LightWord(uint light, uint offset) {
    TraceWords lights = TraceWords(point_lights_address);
    const uint word = (48u * light + offset) >> 2;
    return word < PointLightWords ? lights.words[min(word, PointLightWords - 1u)] : 0u;
}

uint GridWord(uint word) {
    TraceWords grid = TraceWords(light_grid_address);
    return word < LightGridWords ? grid.words[min(word, LightGridWords - 1u)] : 0xffffffffu;
}

// The game's conversion to an unsigned integer: NaN gives 0, other values are clamped to the uint
// range.
uint GameUint(float value) {
    return isnan(value) ? 0u : uint(clamp(value, 0.0, 4294967040.0));
}

// The scene's depth at a pixel, turned back to the game's.
float SceneDepth(uvec2 pixel) {
    return 1.0 - texelFetch(scene_depth, ivec2(pixel), 0).x;
}

// The jitter's noise, from 0 to 1: a hash of the pixel's place.
float TraceNoise(vec2 position) {
    float seed = uintBitsToFloat(0x41b92023u) * position.x;
    seed = position.y * uintBitsToFloat(0x402a91b9u) + seed;
    seed = 256.0 * seed + uintBitsToFloat(0x33d6bf95u);
    const float noise = fract(abs(uintBitsToFloat(0x4ceb79a3u) * (1.0 / seed)));
    return fract(noise * abs(seed));
}

// The steps' shares, 0.1 k as the game's float arithmetic rounds it.
const float StepShare[10] = float[10](0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.90000004);

// How much of a step the scene hides, from 0 to 1: 1 where the scene is level with it or 2/5 or
// more behind it, less in between. The game's program takes the fifth step's x through the
// reciprocal of w and every other place through a division, which round apart.
float StepWeight(TraceConstants trace, vec3 point, bool reciprocal_x) {
    const float w = trace.projection[3] * point.x +
                    (trace.projection[7] * point.y +
                     (trace.projection[11] * point.z + trace.projection[15]));
    const float x = trace.projection[0] * point.x +
                    (trace.projection[4] * point.y +
                     (trace.projection[8] * point.z + trace.projection[12]));
    const float y = -trace.projection[1] * point.x +
                    -(trace.projection[5] * point.y +
                      (trace.projection[9] * point.z + trace.projection[13]));
    const float place_x = reciprocal_x ? fma(x * 0.5, 1.0 / w, 0.5) : 0.5 * (x / w) + 0.5;
    const uvec2 pixel = uvec2(GameUint(trace.target_size[0] * place_x),
                              GameUint(trace.target_size[1] * (0.5 * (y / w) + 0.5)));
    const float scene =
        1.0 / -(trace.inverse_view[2] * (2.0 * SceneDepth(pixel) + -1.0) + trace.inverse_view[3]);
    return min(1.0, abs(5.0 * max(0.0, scene - point.z) + -1.0));
}

// The share of the light the steps toward it find hidden. The steps are a tenth of the way to the
// light, or to the trace's radius where that is nearer, the first moved on by the jitter; nearer
// steps weigh less.
float Occlusion(TraceConstants trace, vec3 position, vec3 toward, float distance2, float noise) {
    const float clamped = max(uintBitsToFloat(0x358637bdu), distance2);
    const vec3 step = toward * ((1.0 / sqrt(clamped)) * min(trace.radius, sqrt(clamped)));
    vec3 point = (0.1 * step) * (trace.jitter * noise) + (0.1 * step + position);
    float total = trace.density;
    for (int k = 1; k < 10; k++) {
        total = total + (StepShare[k] + trace.density);
    }
    float hidden = -trace.density * StepWeight(trace, point, false) + total;
    for (int k = 1; k < 10; k++) {
        point = step * 0.1 + point;
        hidden = -(StepShare[k] + trace.density) * StepWeight(trace, point, k == 4) + hidden;
    }
    return min(1.0, 0.1 * hidden);
}

void main() {
    TraceConstants trace = TraceConstants(constants_address);
    const vec2 place = gl_FragCoord.xy;
    const float depth = SceneDepth(uvec2(GameUint(place.x), GameUint(place.y)));
    const float noise = TraceNoise(place);
    const float across = trace.target_size[2] * place.x;
    const float down = trace.target_size[3] * place.y;

    // The pixel's view position; its distance from the depth, and its slice of the grid from the
    // distance past the near plane, doubling with each slice.
    const float distance =
        1.0 / (trace.inverse_view[2] * (2.0 * depth + -1.0) + trace.inverse_view[3]);
    const float near = 1.0 / (trace.inverse_view[3] - trace.inverse_view[2]);
    const uint slice = uint(clamp(int(GameUint(floor(log2(distance - near)))), 0, 7));
    const uint cell =
        GameUint(60.0 * across) + 60u * GameUint(36.0 * down) + 60u * 36u * slice;
    const float height = trace.inverse_view[1] * distance;
    const vec3 position = vec3((trace.inverse_view[0] * distance) * (2.0 * across + -1.0),
                               -2.0 * (down * height) + height, -distance);

    float shadow = 0.0;
    // A cell holds 48 entries, the last after its lights ~0.
    for (uint i = 0u; i < min(trace.lights, 48u); i++) {
        const uint light = GridWord(((48u * cell + i) << 2) >> 2);
        if (light == 0xffffffffu) {
            break;
        }
        const vec3 toward = uintBitsToFloat(uvec3(LightWord(light, 0u), LightWord(light, 4u),
                                                  LightWord(light, 8u))) -
                            position;
        const float distance2 =
            toward.z * toward.z + (toward.y * toward.y + toward.x * toward.x);
        float occlusion = 0.0;
        if (0.0 < uintBitsToFloat(LightWord(light, LightShadow))) {
            occlusion = Occlusion(trace, position, toward, distance2, noise);
        }
        // The falloff from its start to the light's range, raised to its power.
        const float start = uintBitsToFloat(LightWord(light, LightFalloffStart));
        const float falloff =
            exp2(uintBitsToFloat(LightWord(light, LightFalloffPower)) *
                 log2(abs(min(1.0, (1.0 / (uintBitsToFloat(LightWord(light, LightRange)) - start)) *
                                       max(0.0, sqrt(distance2) - start)))));
        shadow = shadow + occlusion;
        shadow = falloff * -occlusion + shadow;
    }
    const float value = min(1.0, shadow);
    color0 = vec4(unpackHalf2x16(packHalf2x16(vec2(value))), 0.0, 0.0);
}
