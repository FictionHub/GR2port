// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle pixel program 0x1bea3fc, a distortion model: the scene's red, green and blue
// behind the particle, each moved its own distance along its distortion map's direction turned
// into the particle's frame, faded by the depth behind it and coloured by its curve, times its
// alpha.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "particle_distortion.glsl"

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

// The scene's colour at the position moved by the direction in the frame, times the step: x
// along the tangent, y along the normal crossed with the tangent, z along the normal; the move's
// y turned for the texture's.
vec4 FrameTap(vec2 position, vec3 direction, vec3 normal, vec3 tangent, vec2 bitangent,
              float step, vec2 inverse_size) {
    const float along_tangent = direction.x * step;
    const float along_bitangent = direction.y * step;
    const float along_normal = direction.z * step;
    const float x = along_normal * normal.x +
                    (along_bitangent * bitangent.x + (tangent.x * along_tangent + position.x));
    const float y = along_normal * normal.y +
                    (along_bitangent * bitangent.y + tangent.y * along_tangent);
    return textureLod(sampler2D(background_texture, curve_sampler),
                      vec2(inverse_size.x * x, inverse_size.y * (position.y - y)), 0.0);
}

void main() {
    ParticleDraw draw = Draw();
    const vec2 position = GamePosition(draw);
    // The map's x, y and z over the length of all four of its values.
    const vec4 map = DistortionMap(draw, attr2.xy);
    const vec3 direction =
        (1.0 / sqrt(map.w * map.w + (map.z * map.z + (map.y * map.y + map.x * map.x)))) * map.xyz;
    const vec3 normal = attr4.xyz;
    const vec3 tangent = attr5.xyz;
    // The normal crossed with the tangent, x and y.
    const vec2 bitangent = vec2(tangent.y * -normal.z + normal.y * tangent.z,
                                tangent.z * -normal.x + normal.z * tangent.x);
    const vec2 inverse_size =
        vec2(1.0 / draw.constants[ScreenSize], 1.0 / draw.constants[ScreenSize + 1u]);
    // Blue, green and red, each moved ten times its own strength, read in the game's program's
    // order.
    const float blue =
        FrameTap(position, direction, normal, tangent, bitangent, 10.0 * attr0.z, inverse_size).b;
    const float green =
        FrameTap(position, direction, normal, tangent, bitangent, 10.0 * attr0.y, inverse_size).g;
    const float red =
        FrameTap(position, direction, normal, tangent, bitangent, 10.0 * attr0.x, inverse_size).r;
    const float fade = DepthFade(draw, position * inverse_size, attr1.x);
    color0 = DistortedColor(draw, vec3(red, green, blue), attr0.w, fade, attr2.zw, attr3.z,
                            floatBitsToUint(attr6.x));
}
