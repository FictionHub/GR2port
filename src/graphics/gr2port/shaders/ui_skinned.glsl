// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the game's skinned UI programs 0x1b11848 and 0x1b12270 share: the position moved by four
// bones blended by four weights, then through the column-major 4x4 matrix in constants 0-15 with
// the depth at half of w. Each bone is a column-major 4x4 matrix in four rows of the draw's
// palette.

#include "ui_transform.glsl"

// Column `column` of a bone's matrix. The game's program finds the bone's first row from the
// bits of its number, (number << 6) >> 4 as a signed number.
vec4 BoneColumn(UiWords palette, uint bone, uint column) {
    const uint at = ((uint(int(bone << 6) >> 4) + column) * 16u) >> 2;
    return uintBitsToFloat(uvec4(palette.words[at], palette.words[at + 1u], palette.words[at + 2u],
                                 palette.words[at + 3u]));
}

vec4 BlendedColumn(UiWords palette, uvec4 bones, vec4 weights, uint column) {
    return weights.w * BoneColumn(palette, bones.w, column) +
           (weights.z * BoneColumn(palette, bones.z, column) +
            (weights.y * BoneColumn(palette, bones.y, column) +
             BoneColumn(palette, bones.x, column) * weights.x));
}

vec4 SkinnedPosition(UiDraw draw, UiConstants constants, vec3 position, uvec4 bones,
                     vec4 weights) {
    UiWords palette = UiWords(draw.vertex_palette);
    const vec4 moved = BlendedColumn(palette, bones, weights, 0u) * position.x +
                       (BlendedColumn(palette, bones, weights, 1u) * position.y +
                        (BlendedColumn(palette, bones, weights, 2u) * position.z +
                         BlendedColumn(palette, bones, weights, 3u)));
    const vec4 clip = ConstantVec4(constants, 0u) * moved.x +
                      (ConstantVec4(constants, 4u) * moved.y +
                       (ConstantVec4(constants, 8u) * moved.z +
                        ConstantVec4(constants, 12u) * moved.w));
    return ClipPosition(vec4(clip.xy, 0.5 * clip.w, clip.w));
}
