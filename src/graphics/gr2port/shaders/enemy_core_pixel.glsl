// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the nevi core's pixel programs share: Scene::EnemyDraw's pixel constants (Buffer1 in the
// programs' metadata) and mesh, the surface's normal from the normal map turned by the tangent
// frame, and the colour ramp of CoreColor0 to CoreColor2.

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer CoreConstants {
    float constants[28];
};
layout(buffer_reference, std430, buffer_reference_align = 8) readonly buffer EnemyMesh {
    uint64_t vertices;
    uint64_t skin;
    uint64_t palette;
    uint stride;
    uint displacement;
    uint elements[16];
    uint maps[8];
};
const uint64_t EnemyDrawBytes = 896ul;
const uint64_t CorePixelOffset = 272ul;
const uint64_t EnemyMeshOffset = 768ul;
const uint ColorMap = 0u;
const uint NormalMap = 1u;

// u_f3CoreColor0 to 2, each with its u_fCoreColorPos in w, u_fNormalScale, u_fTexScale,
// u_fGlowScale and u_f2PunchThrough.
const uint CoreColor0 = 0u;
const uint CoreColorPos0 = 3u;
const uint CoreColor1 = 4u;
const uint CoreColorPos1 = 7u;
const uint CoreColor2 = 8u;
const uint CoreColorPos2 = 11u;
const uint NormalScale = 12u;
const uint TexScale = 13u;
const uint GlowScale = 14u;
const uint PunchThrough = 24u;

layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler texture_sampler;
layout(push_constant) uniform Push {
    uint64_t draws_address;
};
layout(location = 15) flat in uint draw_index;

uint64_t CoreDrawAddress() {
    return draws_address + uint64_t(draw_index) * EnemyDrawBytes;
}

vec3 CoreColor(CoreConstants constants, uint first) {
    return vec3(constants.constants[first], constants.constants[first + 1u],
                constants.constants[first + 2u]);
}

// The vector over its length, in the order of operations of the game's programs.
vec3 Normalized(vec3 v) {
    return v * (1.0 / sqrt(v.z * v.z + (v.y * v.y + v.x * v.x)));
}

// The surface's normal, not of unit length: the normal map from -1 to 1 over its length, x and y
// times NormalScale, turned by the tangent, the normal crossed with the tangent times the
// tangent's sign, and the normal.
vec3 CoreSurface(CoreConstants constants, vec4 tangent_in, vec3 normal_in, vec3 normal_map) {
    const vec3 tangent = Normalized(tangent_in.xyz);
    const vec3 normal = Normalized(normal_in);
    const float handedness = float((0.0 < tangent_in.w ? 1 : 0) - (0.0 > tangent_in.w ? 1 : 0));
    const vec3 bitangent = vec3(tangent.y * -normal.z + normal.y * tangent.z,
                                tangent.z * -normal.x + normal.z * tangent.x,
                                tangent.x * -normal.y + normal.x * tangent.y) *
                           handedness;
    const vec3 map = 2.0 * normal_map + -1.0;
    const float inverse = 1.0 / sqrt(map.z * map.z + (map.y * map.y + map.x * map.x));
    const float scale = constants.constants[NormalScale] * inverse;
    const vec3 bent = vec3(scale * map.x, scale * map.y, inverse * map.z);
    return bent.z * normal + (bent.y * bitangent + bent.x * tangent);
}

// The colour ramp at x: CoreColor0 below CoreColorPos0, a smooth step to CoreColor1 at
// CoreColorPos1, the step to the power 0.4 on to CoreColor2 at CoreColorPos2, then CoreColor2.
vec3 CoreRamp(CoreConstants constants, float x) {
    const float pos0 = constants.constants[CoreColorPos0];
    const float pos1 = constants.constants[CoreColorPos1];
    const float pos2 = constants.constants[CoreColorPos2];
    vec3 ramp;
    if (pos0 > x) {
        ramp = CoreColor(constants, CoreColor0);
    } else if (pos1 > x) {
        const float inverse = 1.0 / (pos1 - pos0);
        const float t = clamp(-pos0 * inverse + inverse * x, 0.0, 1.0);
        const float step = -2.0 * (t * (t * t)) + 3.0 * (t * t);
        const vec3 from = CoreColor(constants, CoreColor0);
        ramp = (CoreColor(constants, CoreColor1) - from) * step + from;
    } else if (pos2 > x) {
        const float inverse = 1.0 / (pos2 - pos1);
        const float t = clamp(-pos1 * inverse + inverse * x, 0.0, 1.0);
        const float step = exp2(0.4 * log2(abs(-2.0 * (t * (t * t)) + 3.0 * (t * t))));
        const vec3 from = CoreColor(constants, CoreColor1);
        ramp = step * (CoreColor(constants, CoreColor2) - from) + from;
    } else {
        ramp = CoreColor(constants, CoreColor2);
    }
    return ramp;
}
