// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the game's distortion model programs share beside particle_distortion.glsl: the scene's
// red, green and blue each read a step along the distortion map's direction turned into the
// particle's frame.

#include "particle_distortion.glsl"

// The map's x, y and z over the length of all four of its values.
vec3 FrameDirection(vec4 map) {
    return (1.0 / sqrt(map.w * map.w + (map.z * map.z + (map.y * map.y + map.x * map.x)))) *
           map.xyz;
}

// The normal crossed with the tangent, x and y.
vec2 FrameBitangent(vec3 normal, vec3 tangent) {
    return vec2(tangent.y * -normal.z + normal.y * tangent.z,
                tangent.z * -normal.x + normal.z * tangent.x);
}

// The scene's colour at the position moved in the frame: x along the tangent, y along the normal
// crossed with the tangent, z along the normal; the move's y turned for the texture's.
vec4 FrameMoveTap(vec2 position, vec3 move, vec3 normal, vec3 tangent, vec2 bitangent,
                  vec2 inverse_size) {
    const float x = move.z * normal.x + (move.y * bitangent.x + (tangent.x * move.x + position.x));
    const float y = move.z * normal.y + (move.y * bitangent.y + tangent.y * move.x);
    return textureLod(sampler2D(background_texture, curve_sampler),
                      vec2(inverse_size.x * x, inverse_size.y * (position.y - y)), 0.0);
}

// The scene's colour at the position moved by the direction in the frame, times the step.
vec4 FrameTap(vec2 position, vec3 direction, vec3 normal, vec3 tangent, vec2 bitangent,
              float step, vec2 inverse_size) {
    return FrameMoveTap(position, direction * step, normal, tangent, bitangent, inverse_size);
}

// The scene's red, green and blue, each moved ten times its own strength along the map's
// direction in the frame of the normal and tangent; blue is read first, then green and red, as in
// the game's programs.
vec3 FrameScene(vec2 position, vec4 map, vec3 normal, vec3 tangent, vec3 strengths,
                vec2 inverse_size) {
    const vec3 direction = FrameDirection(map);
    const vec2 bitangent = FrameBitangent(normal, tangent);
    const float blue = FrameTap(position, direction, normal, tangent, bitangent,
                                10.0 * strengths.z, inverse_size)
                           .b;
    const float green = FrameTap(position, direction, normal, tangent, bitangent,
                                 10.0 * strengths.y, inverse_size)
                            .g;
    const float red = FrameTap(position, direction, normal, tangent, bitangent,
                               10.0 * strengths.x, inverse_size)
                          .r;
    return vec3(red, green, blue);
}
