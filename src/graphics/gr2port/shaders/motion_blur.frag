// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's MotionBlur reconstruction (PS 0x1bbe894): where the tile's neighbour maximum moves a
// pixel or more, four taps along it at -1, -0.5, 0.5 and 1 of its length (at most 5 pixels),
// each weighted by its speed and depth against the centre's. Each float step follows the
// program's own, and the result goes through half floats as the program's export does.
#version 460
#extension GL_EXT_buffer_reference : require

layout(set = 0, binding = 0) uniform texture2D color_texture;
layout(set = 0, binding = 1) uniform texture2D neighbor_texture;
layout(set = 0, binding = 2) uniform texture2D compressed_texture;
layout(set = 0, binding = 3) uniform sampler linear_sampler;
layout(set = 0, binding = 4) uniform sampler point_sampler;
layout(set = 0, binding = 5) uniform texture2D scene_depth;
layout(set = 0, binding = 6) uniform texture2D character_depth;

// The compression's constants, then the reconstruction's.
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer Constants {
    vec4 size;
    vec2 inverse_z;
    float velocity_scale;
    layout(offset = 80) vec4 target_size;
};
layout(push_constant) uniform Push {
    Constants constants;
};

// Motion blur Low: one reconstruction instead of the game's two, its taps twice as far apart,
// with no compressed image: binding 2 holds the motion, compressed at each tap.
layout(constant_id = 0) const bool Low = false;

layout(location = 0) in vec4 attr0;
layout(location = 0) out vec4 color0;

#define COMPRESSED sampler2D(compressed_texture, point_sampler)

// A tap's weight: a cone and a cylinder of its speed and the centre's, at the tap's distance,
// split by whether the tap lies in front of the centre or behind it.
float Weight(float distance, vec2 tap, float slope, float near_edge, float depth) {
    const float inverse_speed = 1.0 / (32.0 * tap.x);
    const float front = clamp(32.0 * -tap.y + near_edge, 0.0, 1.0);
    const float cone = clamp(-distance * inverse_speed + 1.0, 0.0, 1.0);
    const float cylinder = clamp(-distance * inverse_speed + uintBitsToFloat(0x3ff9999au), 0.0,
                                 1.0);
    const float back = clamp(1.0 + (32.0 * tap.y + -depth), 0.0, 1.0);
    const float centre = slope * distance + 1.0;
    const float centre_cone = clamp(max(0.0, centre), 0.0, 1.0);
    const float centre_cylinder = clamp(uintBitsToFloat(0x3f733333u) + centre, 0.0, 1.0);
    const float spread = back * centre_cone + front * cone;
    return 2.0 * (cylinder * centre_cylinder) + spread;
}

// The speed and depth term of a motion and the port's reversed depth, as
// motion_blur_compress.comp stores them.
vec2 Compress(vec2 velocity, float reversed_depth) {
    const float depth = 1.0 - reversed_depth;
    const vec2 size = constants.size.xy;
    const float scale = constants.velocity_scale;
    const float moved_y = (scale * size.y) * velocity.y;
    const float moved_x = ((size.x * scale) * velocity.x) * 0.5;
    const float half_y = -0.5 * moved_y;
    const float length2 = half_y * half_y + moved_x * moved_x;
    const float view_z = constants.inverse_z.x * (2.0 * depth + -1.0) + constants.inverse_z.y;
    const float speed = 0.03125 * sqrt(max(uintBitsToFloat(0x358637bdu), length2));
    const float reach = min(1.0, 0.03125 * (1.0 / view_z));
    return unpackHalf2x16(packHalf2x16(vec2(speed, reach)));
}

// The compressed values at a tap.
vec2 Compressed(vec2 position) {
    const vec2 read = textureLod(COMPRESSED, position, 0.0).xy;
    return Low ? Compress(read, textureLod(sampler2D(scene_depth, point_sampler), position, 0.0).x)
               : read;
}

void main() {
    // The game's stencil keeps the characters out of the blur (reference and mask 0x21, not
    // equal): a pixel where the characters' depth is the scene's keeps its colour.
    const ivec2 pixel = ivec2(gl_FragCoord.xy);
    const float character = texelFetch(sampler2D(character_depth, point_sampler), pixel, 0).r;
    if (character > 0.0 &&
        character >= texelFetch(sampler2D(scene_depth, point_sampler), pixel, 0).r) {
        color0 = texelFetch(sampler2D(color_texture, point_sampler), pixel, 0);
        return;
    }

    const vec2 position = attr0.xy;
    const vec2 neighbor =
        textureLod(sampler2D(neighbor_texture, point_sampler), position, 0.0).xy;
    vec4 color = texelFetch(sampler2D(color_texture, point_sampler), pixel, 0);
    const float length2 = max(uintBitsToFloat(0x358637bdu),
                              neighbor.y * neighbor.y + neighbor.x * neighbor.x);
    const float neighbor_length = sqrt(length2);
    if (!(neighbor_length < 1.0)) {
        vec2 step_scale = constants.target_size.zw;
        if (Low) {
            step_scale = 2.0 * step_scale;
        }
        const float reach = (1.0 / sqrt(length2)) * min(5.0, neighbor_length);
        const vec2 moved = step_scale * neighbor;
        const vec2 read = texelFetch(COMPRESSED, pixel, 0).xy;
        const vec2 centre =
            Low ? Compress(read, texelFetch(sampler2D(scene_depth, point_sampler), pixel, 0).x)
                : read;
        const vec2 step = moved * reach;
        const vec2 tap_a = vec2(-moved.x * reach + position.x, -moved.y * reach + position.y);
        const vec2 tap_b = vec2(-0.5 * step.x + position.x, -0.5 * step.y + position.y);
        const vec2 tap_c = vec2(0.5 * step.x + position.x, 0.5 * step.y + position.y);
        const vec2 tap_d = vec2(moved.x * reach + position.x, moved.y * reach + position.y);
        const vec2 compressed_a = Compressed(tap_a);
        const vec2 compressed_b = Compressed(tap_b);
        const vec2 compressed_c = Compressed(tap_c);
        const vec2 compressed_d = Compressed(tap_d);
        const vec4 color_a = textureLod(sampler2D(color_texture, linear_sampler), tap_a, 0.0);
        const vec4 color_b = textureLod(sampler2D(color_texture, linear_sampler), tap_b, 0.0);
        const vec4 color_c = textureLod(sampler2D(color_texture, linear_sampler), tap_c, 0.0);
        const vec4 color_d = textureLod(sampler2D(color_texture, linear_sampler), tap_d, 0.0);

        const float speed = 32.0 * centre.x;
        const float slope = 1.0 / -speed;
        const float near_edge = 32.0 * centre.y + 1.0;
        const float depth = 32.0 * centre.y;
        const float far = abs(speed);
        const float half_far = abs(16.0 * centre.x);
        const float weight_a = Weight(far, compressed_a, slope, near_edge, depth);
        const float weight_b = Weight(half_far, compressed_b, slope, near_edge, depth);
        const float weight_c = Weight(half_far, compressed_c, slope, near_edge, depth);
        const float weight_d = Weight(far, compressed_d, slope, near_edge, depth);
        const float weight = 1.0 / max(0.5, speed);
        vec4 sum = weight * color;
        sum = weight_a * color_a + sum;
        sum = weight_b * color_b + sum;
        sum = weight_c * color_c + sum;
        sum = weight_d * color_d + sum;
        const float total = (((weight + weight_a) + weight_b) + weight_c) + weight_d;
        color = (1.0 / total) * sum;
    }
    color0 = vec4(unpackHalf2x16(packHalf2x16(color.rg)), unpackHalf2x16(packHalf2x16(color.ba)));
}
