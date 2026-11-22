// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the boss variant's nevi body vertex programs read beside enemy_body.glsl: texture
// coordinates 1, which place the displacement, and the binormal.

#include "enemy_body.glsl"

const uint SemanticTexcoord1 = 9u;
const uint SemanticBinormal = 15u;

// The 16-bit integer texture coordinates in an element's words, scaled by 1/1024.
vec2 TexcoordOf(uvec4 words) {
    return vec2(ivec2(words.xy)) * (1.0 / 1024.0);
}
