// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's surface blur pixel program 0x1bc0238 (SurfaceBlur), which blurs the metaballs'
// normals along an axis: thirteen packed normals 2 pixels apart around the pixel, each turned back
// toward the sphere and weighted, their sum packed again. The pixel's own z and w pass through.
#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(set = 0, binding = 0) uniform texture2D source_texture;
layout(set = 0, binding = 1) uniform sampler source_sampler;
// Set where the metaballs drew, standing for the game's stencil test of 0x11.
layout(set = 0, binding = 2) uniform texture2D mask_texture;
// The axis, across or down.
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer Axis {
    ivec2 step;
};
layout(push_constant) uniform Push {
    uint64_t axis_address;
};
layout(location = 0) out vec4 color0;

// The game's weights 2 pixels out to 12 on either side, written as the exact floats it holds
// (0x3fe66666, 0x3fcccccc, 0x3fb33333, 0x3f999999, 0x3f7ffffe and 0x3f4ccccc), some of them a bit
// under the nearest floats to 1.6, 1.2, 1 and 0.8.
const float Weight2 = 1.7999999523162842;
const float Weight4 = 1.5999999046325684;
const float Weight6 = 1.399999976158142;
const float Weight8 = 1.1999999284744263;
const float Weight10 = 0.9999998807907104;
const float Weight12 = 0.7999999523162842;
// The z sum's offset, 0xc10ccccc.
const float ZOffset = -8.799999237060547;

vec2 Packed(ivec2 place) {
    return texelFetch(sampler2D(source_texture, source_sampler), place, 0).xy;
}

// A packed normal turned back: x and y times sqrt(1 - r^2), and r^2, the square of their length.
vec3 Unpacked(vec2 packed) {
    const float r2 = packed.y * packed.y + packed.x * packed.x;
    const float s = sqrt(1.0 - r2);
    return vec3(packed.x * s, packed.y * s, r2);
}

// A tap added to the sums of x, y and 1 - r^2, each weighted, in the game's program's order.
void Add(inout vec3 sum, vec3 tap, float weight) {
    sum.x = tap.x * weight + sum.x;
    sum.y = tap.y * weight + sum.y;
    sum.z = sum.z + fma(-tap.z, weight, weight);
}

// The G-buffer's packed normal: x and y over their length, times sqrt(0.5 z + 0.5) of the normal
// with z turned.
vec2 PackNormal(vec3 normal) {
    const float inverse =
        1.0 / sqrt(normal.z * normal.z + (normal.y * normal.y + normal.x * normal.x));
    const float x = inverse * normal.x;
    const float y = inverse * normal.y;
    const float z = -inverse * normal.z;
    const float xy2 = max(uintBitsToFloat(0x358637bdu), y * y + x * x);
    const float scale = 1.0 / sqrt(xy2) * sqrt(abs(0.5 * z + 0.5));
    return vec2(scale * x, scale * y);
}

void main() {
    if (texelFetch(sampler2D(mask_texture, source_sampler), ivec2(gl_FragCoord.xy), 0).r == 0.0) {
        discard;
    }
    const ivec2 pixel = ivec2(gl_FragCoord.xy);
    const ivec2 step = Axis(axis_address).step;
    // Every tap is read before the sums.
    const vec4 centre = texelFetch(sampler2D(source_texture, source_sampler), pixel, 0);
    const vec2 before10 = Packed(pixel + step * -10);
    const vec2 before8 = Packed(pixel + step * -8);
    const vec2 before6 = Packed(pixel + step * -6);
    const vec2 before4 = Packed(pixel + step * -4);
    const vec2 before2 = Packed(pixel + step * -2);
    const vec2 after2 = Packed(pixel + step * 2);
    const vec2 after4 = Packed(pixel + step * 4);
    const vec2 after6 = Packed(pixel + step * 6);
    const vec2 after8 = Packed(pixel + step * 8);
    const vec2 after10 = Packed(pixel + step * 10);
    const vec2 before12 = Packed(pixel + step * -12);
    const vec2 after12 = Packed(pixel + step * 12);

    // The pixel weighs 2; the taps from 10 pixels before it to 10 after are added in turn.
    const vec3 own = Unpacked(centre.xy);
    vec3 sum = vec3(own.x * 2.0, own.y * 2.0, fma(-own.z, 2.0, 2.0));
    Add(sum, Unpacked(before10), Weight10);
    Add(sum, Unpacked(before8), Weight8);
    Add(sum, Unpacked(before6), Weight6);
    Add(sum, Unpacked(before4), Weight4);
    Add(sum, Unpacked(before2), Weight2);
    Add(sum, Unpacked(after2), Weight2);
    Add(sum, Unpacked(after4), Weight4);
    Add(sum, Unpacked(after6), Weight6);
    Add(sum, Unpacked(after8), Weight8);
    Add(sum, Unpacked(after10), Weight10);

    // The taps 12 pixels away are summed as a pair first, and z loses 8.8.
    const vec3 before = Unpacked(before12);
    const float after_rest = 1.0 - (after12.y * after12.y + after12.x * after12.x);
    const float after_s = sqrt(after_rest);
    sum.x = (after12.x * after_s + before.x) * Weight12 + sum.x;
    sum.y = (after12.y * after_s + before.y) * Weight12 + sum.y;
    sum.z = ((1.0 - before.z) + after_rest) * Weight12 + (ZOffset + sum.z);

    color0 = vec4(unpackHalf2x16(packHalf2x16(PackNormal(sum))),
                  unpackHalf2x16(packHalf2x16(centre.zw)));
}
