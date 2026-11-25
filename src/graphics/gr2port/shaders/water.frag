// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's water pixel program 0x1ba508c: the surface's normal bent by the bubbles (spheres)
// the pixel's ray from the camera ends in, then by a normal map read through a flow map; the cube
// map's reflection with its Fresnel weight; the scene behind refracted through the surface; up to
// three colour maps blended in, lit by the sun (through the shadow cascades) and the point lights
// with stepped toning; over the scene by the alpha, under the reflection, into the distance fog.
// The depth is 1 less the bubbles' depth.
#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler texture_sampler;
layout(set = 0, binding = 0) uniform textureCube cubes[];
layout(set = 0, binding = 2) uniform sampler clamp_sampler;
layout(set = 1, binding = 0) uniform sampler2DArrayShadow shadow_map;
layout(set = 1, binding = 2) uniform sampler2DShadow last_shadow_map;
layout(set = 2, binding = 0) uniform texture2D depth_texture;
layout(set = 2, binding = 1) uniform sampler depth_sampler;
layout(set = 2, binding = 2) uniform texture2D background_texture;
layout(set = 2, binding = 3) uniform sampler background_sampler;

layout(push_constant) uniform Push {
    uint64_t draws_address;
    uint64_t lights_address;
    uint64_t point_lights_address;
    uint64_t light_grid_address;
};

#include "particle_lit.glsl"

// The vertex colour; the tangent, and its w; the three shadow cascades' places (x, y, depth, w);
// the normal, and the world position's x; the world position's y and z, and the view position's
// x and y; the view position's z, and the texture coordinates.
layout(location = 15) flat in uint draw_index;
layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 2) in vec4 attr2;
layout(location = 3) in vec4 attr3;
layout(location = 4) in vec4 attr4;
layout(location = 5) in vec4 attr5;
layout(location = 6) in vec4 attr6;
layout(location = 7) in vec4 attr7;
layout(location = 0) out vec4 color0;

// The water draw (1200 bytes): the pixel constants from word 152, the mesh's colour maps from
// word 294 and its reflection cube at word 299.
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer WaterDrawWords {
    uint words[];
};

WaterDrawWords WaterDraw() {
    return WaterDrawWords(draws_address + uint64_t(draw_index) * 1200ul);
}

float WaterConstant(uint word) {
    return uintBitsToFloat(WaterDraw().words[152u + word]);
}

uint WaterMap(uint map) {
    return WaterDraw().words[294u + map];
}

// The pixel constants by word: the cascades' far depths (1, 2), the game's size and its inverse
// (4 to 7), per colour map a blend method, colour scale, alpha scale and flow scale (8, 12, 16,
// 20 + map) and its offset and repeat (24 + 4 map), the flow map's (36) and the normal map's (40)
// offset and repeat, the light's colour scale (44), the alpha scale (45), the base reflectance
// (46), the softness (47), the inverse view's depth terms (48, 49), the flow's normal scale (50),
// the flags (51), the light's colour (52) and direction (56), the count of a cell's point lights
// (63), the view rows (64), the view-projection rows (80), the bubbles' rows (96), the camera
// (112), the count of bubbles (115) and the bubbles (116 + 4 k), the fog's near and far (124).
const uint WaterFlagReflect = 0x8u;
const uint WaterFlagRefract = 0x10u;
const uint WaterFlagSoft = 0x4u;

// The game's cube addressing: the major axis (twice the coordinate), the face and the face's s
// and t, and the direction rebuilt from them.
float CubeMajor(vec3 v) {
    const vec3 a = abs(v);
    return a.z >= a.x && a.z >= a.y ? 2.0 * v.z : a.y >= a.x ? 2.0 * v.y : 2.0 * v.x;
}

float CubeFace(vec3 v) {
    const vec3 a = abs(v);
    if (a.z >= a.x && a.z >= a.y) {
        return v.z < 0.0 ? 5.0 : 4.0;
    }
    return a.y >= a.x ? (v.y < 0.0 ? 3.0 : 2.0) : (v.x < 0.0 ? 1.0 : 0.0);
}

float CubeS(vec3 v) {
    const vec3 a = abs(v);
    if (a.z >= a.x && a.z >= a.y) {
        return v.z < 0.0 ? -v.x : v.x;
    }
    return a.y >= a.x ? v.x : (v.x < 0.0 ? v.z : -v.z);
}

float CubeT(vec3 v) {
    const vec3 a = abs(v);
    if (a.z >= a.x && a.z >= a.y) {
        return -v.y;
    }
    return a.y >= a.x ? (v.y < 0.0 ? -v.z : v.z) : -v.y;
}

vec3 CubeDirection(float s, float t, float face) {
    const float a = 2.0 * (s - 1.5);
    const float b = 2.0 * (t - 1.5);
    switch (int(face + 0.5)) {
    case 0:
        return vec3(1.0, -b, -a);
    case 1:
        return vec3(-1.0, -b, a);
    case 2:
        return vec3(a, 1.0, b);
    case 3:
        return vec3(a, -1.0, -b);
    case 4:
        return vec3(a, -b, 1.0);
    default:
        return vec3(-a, -b, -1.0);
    }
}

// A colour map's colour and alpha (scaled by its words 12 + map and 16 + map) at the coordinates
// pushed by the flow (word 20 + map) through its offset and repeat (24 + 4 map).
vec4 WaterMapColor(uint map, vec2 uv, vec2 flow) {
    const float push = WaterConstant(20u + map);
    const float u = push * flow.x + uv.x;
    const float v = push * flow.y + uv.y;
    const uint words = 24u + 4u * map;
    const vec4 texel =
        textureLod(sampler2D(textures[nonuniformEXT(WaterMap(map))], texture_sampler),
                   vec2(WaterConstant(words + 2u) * u + WaterConstant(words),
                        WaterConstant(words + 3u) * v + WaterConstant(words + 1u)),
                   0.0);
    const float scale = WaterConstant(12u + map);
    return vec4(scale * texel.x, scale * texel.y, scale * texel.z,
                WaterConstant(16u + map) * texel.w);
}

// Map 0 over white with alpha 1 (the game's program folds them in), by its method (word 8):
// 1 a mix, 2 an add, 3 and 4 the map, 5 a mix of the colour only, 6 an add of the colour only,
// 7 the map's colour only, else white.
vec4 WaterFirstLayer(vec4 c) {
    const int method = int(floatBitsToUint(WaterConstant(8u)));
    const float a = c.w;
    if (method == 1) {
        const float rest = 1.0 - a;
        return vec4(c.x * a + rest, c.y * a + rest, c.z * a + rest, a * a + rest);
    }
    if (method == 2) {
        return vec4(1.0 + c.x, 1.0 + c.y, 1.0 + c.z, 1.0 + a);
    }
    if (method == 3 || method == 4) {
        return c;
    }
    if (method == 5) {
        const float rest = 1.0 - a;
        return vec4(a * c.x + rest, c.y * a + rest, c.z * a + rest, 1.0);
    }
    if (method == 6) {
        return vec4(1.0 + c.x, 1.0 + c.y, 1.0 + c.z, 1.0);
    }
    if (method == 7) {
        return vec4(c.xyz, 1.0);
    }
    return vec4(1.0);
}

// Maps 1 and 2 over the colour and alpha so far, by their methods (words 9 and 10): 1 a mix, 2 an
// add, 3 a product, 4 the map, 5 to 7 as 1 to 3 for the colour only, else white.
vec4 WaterNextLayer(vec4 base, vec4 c, int method) {
    const float a = c.w;
    if (method == 1) {
        const float rest = 1.0 - a;
        const float aa = a * a;
        const vec3 ca = vec3(c.x * a, c.y * a, c.z * a);
        return vec4(base.x * rest + ca.x, base.y * rest + ca.y, base.z * rest + ca.z,
                    base.w * rest + aa);
    }
    if (method == 2) {
        return vec4(base.x + c.x, base.y + c.y, base.z + c.z, base.w + a);
    }
    if (method == 3) {
        return vec4(base.x * c.x, base.y * c.y, base.z * c.z, base.w * a);
    }
    if (method == 4) {
        return c;
    }
    if (method == 5) {
        const float rest = 1.0 - a;
        const vec3 ca = vec3(c.x * a, c.y * a, c.z * a);
        return vec4(base.x * rest + ca.x, base.y * rest + ca.y, base.z * rest + ca.z, base.w);
    }
    if (method == 6) {
        return vec4(base.x + c.x, base.y + c.y, base.z + c.z, base.w);
    }
    if (method == 7) {
        return vec4(base.x * c.x, base.y * c.y, base.z * c.z, base.w);
    }
    return vec4(1.0);
}

// The scene's view depth at a pixel of the game's size.
float WaterSceneDepth(ivec2 pixel, vec2 fetch_scale) {
    const float stored = 1.0 - texelFetch(sampler2D(depth_texture, depth_sampler),
                                          ivec2((vec2(pixel) + 0.5) * fetch_scale), 0)
                                   .x;
    return 1.0 / -(WaterConstant(48u) * (2.0 * stored + -1.0) + WaterConstant(49u));
}

void main() {
    const vec2 game = vec2(WaterConstant(4u), WaterConstant(5u));
    const vec2 target = vec2(textureSize(sampler2D(depth_texture, depth_sampler), 0));
    const vec2 fetch_scale = target / game;
    const vec2 place =
        vec2(gl_FragCoord.x * game.x / target.x, gl_FragCoord.y * game.y / target.y);
    const float depth = 1.0 - gl_FragCoord.z;
    const float clip_w = 1.0 / gl_FragCoord.w;
    float out_depth = depth;
    const uint flags = floatBitsToUint(WaterConstant(51u));

    // The surface's normal, bent toward the far side of the bubble the camera's ray through the
    // pixel ends in deepest (by up to 5 times the ray's rise there), with that bubble's depth.
    const vec3 raw_normal = attr5.xyz;
    const float normal_inverse = 1.0 / sqrt(raw_normal.z * raw_normal.z +
                                            (raw_normal.y * raw_normal.y +
                                             raw_normal.x * raw_normal.x));
    vec3 n = vec3(raw_normal.x * normal_inverse, raw_normal.y * normal_inverse,
                  raw_normal.z * normal_inverse);
    const vec3 world = vec3(attr5.w, attr6.x, attr6.y);
    const uint bubbles = floatBitsToUint(WaterConstant(115u));
    if (bubbles > 0u) {
        const vec3 camera = vec3(WaterConstant(112u), WaterConstant(113u), WaterConstant(114u));
        const float ndc = 2.0 * depth + -1.0;
        const vec3 to_world = vec3(world.x - camera.x, world.y - camera.y, world.z - camera.z);
        const float ray_inverse = 1.0 / sqrt(to_world.z * to_world.z +
                                             (to_world.y * to_world.y +
                                              to_world.x * to_world.x));
        const float own_term = WaterConstant(48u) * ndc + WaterConstant(49u);
        const vec3 ray = vec3(ray_inverse * to_world.x, ray_inverse * to_world.y,
                              ray_inverse * to_world.z);
        const float own = 1.0 / -own_term;
        const float to_ndc = 1.0 / WaterConstant(48u);
        float deepest = 0.0;
        // The record holds two barriers.
        for (uint k = 0u; k < min(bubbles, 2u); k++) {
            const uint at = 116u + 4u * k;
            const vec4 bubble = vec4(WaterConstant(at), WaterConstant(at + 1u),
                                     WaterConstant(at + 2u), WaterConstant(at + 3u));
            const float dx = bubble.x - world.x;
            float d2 = dx * dx;
            const float dy = bubble.y - world.y;
            d2 = dy * dy + d2;
            const float dz = bubble.z - world.z;
            d2 = dz * dz + d2;
            if (bubble.w > sqrt(d2)) {
                const vec3 from = vec3(world.x - bubble.x, world.y - bubble.y,
                                       world.z - bubble.z);
                const float along = ray.z * from.z + (ray.y * from.y + from.x * ray.x);
                const float px = along * -ray.x + from.x;
                float off2 = px * px;
                const float py = along * -ray.y + from.y;
                off2 = py * py + off2;
                const float pz = along * -ray.z + from.z;
                off2 = pz * pz + off2;
                const float off = sqrt(off2);
                const float t = sqrt(off * -off + bubble.w * bubble.w) - along;
                if (t > deepest) {
                    const vec3 hit = vec3(ray.x * t + world.x, ray.y * t + world.y,
                                          ray.z * t + world.z);
                    const vec3 to_centre = vec3(bubble.x - hit.x, bubble.y - hit.y,
                                                bubble.z - hit.z);
                    const float centre_inverse =
                        1.0 / sqrt(to_centre.z * to_centre.z +
                                   (to_centre.y * to_centre.y + to_centre.x * to_centre.x));
                    const vec3 inside = vec3(centre_inverse * to_centre.x,
                                             centre_inverse * to_centre.y,
                                             centre_inverse * to_centre.z);
                    const float r0 = WaterConstant(96u) * inside.x +
                                     (WaterConstant(100u) * inside.y +
                                      WaterConstant(104u) * inside.z);
                    const float r1 = WaterConstant(97u) * inside.x +
                                     (WaterConstant(101u) * inside.y +
                                      WaterConstant(105u) * inside.z);
                    const float r2 = WaterConstant(98u) * inside.x +
                                     (WaterConstant(102u) * inside.y +
                                      WaterConstant(106u) * inside.z);
                    const float bend = min(1.0, 5.0 * abs(ray.y * t));
                    const float mx = bend * (r0 - raw_normal.x) + raw_normal.x;
                    const float my = bend * (r1 - raw_normal.y) + raw_normal.y;
                    const float mz = bend * (r2 - raw_normal.z) + raw_normal.z;
                    const float bent_inverse = 1.0 / sqrt(mz * mz + (my * my + mx * mx));
                    const float moved = (1.0 / -(own - t)) * to_ndc;
                    const float moved_ndc = -WaterConstant(49u) * to_ndc + moved;
                    n = vec3(bent_inverse * mx, bent_inverse * my, bent_inverse * mz);
                    out_depth = 0.5 * moved_ndc + 0.5;
                    deepest = t;
                }
            }
        }
    }

    // The flow map (map 3) at the texture coordinates through its offset and repeat (36).
    const vec2 uv = attr7.yz;
    const vec4 flow_texel =
        textureLod(sampler2D(textures[nonuniformEXT(WaterMap(3u))], texture_sampler),
                   vec2(WaterConstant(38u) * uv.x + WaterConstant(36u),
                        WaterConstant(39u) * uv.y + WaterConstant(37u)),
                   0.0);
    const vec2 flow = flow_texel.xy;

    // The tangent frame: the tangent less the tangent times its dot with the normal (as the
    // game's program has it), unit length, and the bitangent n x t times the tangent's w.
    const vec3 raw_tangent = attr1.xyz;
    const float tangent_inverse = 1.0 / sqrt(raw_tangent.z * raw_tangent.z +
                                             (raw_tangent.y * raw_tangent.y +
                                              raw_tangent.x * raw_tangent.x));
    const vec3 t0 = vec3(raw_tangent.x * tangent_inverse, raw_tangent.y * tangent_inverse,
                         raw_tangent.z * tangent_inverse);
    const float t_dot = t0.z * n.z + (t0.y * n.y + n.x * t0.x);
    const vec3 t1 = vec3(t_dot * -t0.x + t0.x, t_dot * -t0.y + t0.y, t_dot * -t0.z + t0.z);
    const float t1_inverse = 1.0 / sqrt(t1.z * t1.z + (t1.y * t1.y + t1.x * t1.x));
    const vec3 tangent = vec3(t1_inverse * t1.x, t1_inverse * t1.y, t1_inverse * t1.z);
    const float side = attr1.w;
    const vec3 bitangent = vec3(side * (tangent.y * -n.z + n.y * tangent.z),
                                side * (tangent.z * -n.x + n.z * tangent.x),
                                side * (tangent.x * -n.y + n.x * tangent.y));
    const vec3 view = vec3(attr6.z, attr6.w, attr7.x);

    // The normal map (map 4) at the coordinates pushed by the flow (word 50), its offset and
    // repeat (40), turned into the frame.
    const float flow_x = WaterConstant(50u) * (2.0 * flow.x + -1.0) + uv.x;
    const float flow_y = WaterConstant(50u) * (2.0 * flow.y + -1.0) + uv.y;
    const vec4 normal_texel =
        textureLod(sampler2D(textures[nonuniformEXT(WaterMap(4u))], texture_sampler),
                   vec2(WaterConstant(42u) * flow_x + WaterConstant(40u),
                        WaterConstant(43u) * flow_y + WaterConstant(41u)),
                   0.0);
    const float bx = 2.0 * normal_texel.x + -1.0;
    const float by = 2.0 * normal_texel.y + -1.0;
    const float bz = 2.0 * normal_texel.z + -1.0;
    const float bump_inverse = 1.0 / sqrt(bz * bz + (by * by + bx * bx));
    const float ux = bump_inverse * bx;
    const float uy = bump_inverse * by;
    const float uz = bump_inverse * bz;
    const vec3 surface = vec3(uz * n.x + (bitangent.x * uy + ux * tangent.x),
                              uz * n.y + (uy * bitangent.y + ux * tangent.y),
                              n.z * uz + (uy * bitangent.z + ux * tangent.z));

    // The cube map's reflection of the view about the normal in view space, at level 6, with
    // its Fresnel weight from the base reflectance (word 46).
    vec3 reflection = vec3(0.0);
    vec3 fresnel = vec3(0.0);
    if ((flags & WaterFlagReflect) != 0u) {
        const float facing = surface.z * view.z + (surface.y * view.y + view.x * surface.x);
        const vec3 rn = vec3(WaterConstant(64u) * surface.x +
                                 (WaterConstant(68u) * surface.y + WaterConstant(72u) * surface.z),
                             WaterConstant(65u) * surface.x +
                                 (WaterConstant(69u) * surface.y + WaterConstant(73u) * surface.z),
                             WaterConstant(66u) * surface.x +
                                 (WaterConstant(70u) * surface.y + WaterConstant(74u) * surface.z));
        const vec3 rv = vec3(WaterConstant(64u) * view.x +
                                 (WaterConstant(68u) * view.y + WaterConstant(72u) * view.z),
                             view.x * WaterConstant(65u) +
                                 (WaterConstant(69u) * view.y + WaterConstant(73u) * view.z),
                             WaterConstant(66u) * view.x +
                                 (view.y * WaterConstant(70u) + WaterConstant(74u) * view.z));
        const float rn_inverse = 1.0 / sqrt(rn.z * rn.z + (rn.y * rn.y + rn.x * rn.x));
        const float rv_inverse = 1.0 / sqrt(rv.z * rv.z + (rv.y * rv.y + rv.x * rv.x));
        const vec3 un = vec3(rn_inverse * rn.x, rn_inverse * rn.y, rn_inverse * rn.z);
        const vec3 uvw = vec3(rv_inverse * rv.x, rv_inverse * rv.y, rv_inverse * rv.z);
        const float twice = (un.z * uvw.z + (un.y * uvw.y + uvw.x * un.x)) * 2.0;
        const vec3 r = vec3(twice * -un.x + uvw.x, twice * -un.y + uvw.y, twice * -un.z + uvw.z);
        const float major = 1.0 / abs(CubeMajor(r));
        const vec3 direction = CubeDirection(CubeS(r) * major + 1.5, CubeT(r) * major + 1.5,
                                             CubeFace(r));
        const uint cube = WaterDraw().words[299];
        const vec4 sky = cube == 0u ? vec4(0.0)
                                    : textureLod(samplerCube(cubes[nonuniformEXT(cube)],
                                                             clamp_sampler),
                                                 direction, 6.0);
        const float edge = log2(clamp(max(0.0, 1.0 + facing), 0.0, 1.0));
        const float base = WaterConstant(46u);
        const float k = base * uintBitsToFloat(0x3ee7a0f7u) +
                        uintBitsToFloat(0xbb1d494cu) * clamp(base * 50.0, 0.0, 1.0);
        const float weight = exp2(5.0 * edge) * (1.0 - base) + base;
        fresnel = vec3(weight);
        reflection = vec3(sky.x * k, sky.y * k, sky.z * k);
    }

    // The scene behind: its view depth at the pixel, and where the refraction asks the place the
    // view refracted through the surface (eta 0.75) reaches, kept where the scene there lies
    // below the water.
    const float scene = WaterSceneDepth(ivec2(int(LightUint(place.x)), int(LightUint(place.y))),
                                        fetch_scale);
    const vec2 screen = vec2(WaterConstant(6u) * place.x, WaterConstant(7u) * place.y);
    vec2 behind = screen;
    if ((flags & WaterFlagRefract) != 0u) {
        const float view_inverse =
            1.0 / sqrt(view.z * view.z + (view.y * view.y + view.x * view.x));
        const vec3 v = vec3(view.x * view_inverse, view.y * view_inverse, view.z * view_inverse);
        const float c = surface.z * v.z + (v.y * surface.y + surface.x * v.x);
        const float k = uintBitsToFloat(0x3f101271u) * -(c * c) + uintBitsToFloat(0x3f101271u);
        const float m = uintBitsToFloat(0x3f400c4bu) * -c + -sqrt(abs(1.0 - k));
        const float gate = 1.0 > k ? 1.0 : 0.0;
        const float rz = gate * (surface.z * m + uintBitsToFloat(0x3f400c4bu) * v.z);
        const float ry = gate * (surface.y * m + uintBitsToFloat(0x3f400c4bu) * v.y);
        const float rx = gate * (surface.x * m + uintBitsToFloat(0x3f400c4bu) * v.x);
        const float scene_height =
            (WaterConstant(65u) * view.x + WaterConstant(69u) * view.y) *
                ((1.0 / view.z) * scene) +
            (WaterConstant(73u) * scene + WaterConstant(77u));
        const float rise = WaterConstant(65u) * rx +
                           (WaterConstant(69u) * ry + WaterConstant(73u) * rz);
        const float reach =
            (1.0 / -rise) * min(5.0, WaterConstant(55u) * (world.y - scene_height));
        const vec3 p = vec3(rx * reach + view.x, ry * reach + view.y, rz * reach + view.z);
        const float w = WaterConstant(83u) * p.x +
                        (WaterConstant(87u) * p.y + (WaterConstant(91u) * p.z +
                                                     WaterConstant(95u)));
        const float x = WaterConstant(80u) * p.x +
                        (WaterConstant(84u) * p.y + (WaterConstant(88u) * p.z +
                                                     WaterConstant(92u)));
        const float z = WaterConstant(82u) * p.x +
                        (WaterConstant(86u) * p.y + (WaterConstant(90u) * p.z +
                                                     WaterConstant(94u)));
        const float to_w = 1.0 / w;
        const float nx = to_w * x;
        const float ny = -2.0 * screen.y + 1.0;
        const float nz = to_w * z;
        if (1.0 >= max(max(abs(nx), abs(ny)), abs(nz))) {
            const vec2 moved = vec2(0.5 * nx + 0.5, -0.5 * ny + 0.5);
            const float there = WaterSceneDepth(
                ivec2(int(LightUint(WaterConstant(4u) * moved.x)),
                      int(LightUint(WaterConstant(5u) * moved.y))),
                fetch_scale);
            const float height = (WaterConstant(65u) * p.x + WaterConstant(69u) * p.y) *
                                     ((1.0 / p.z) * there) +
                                 (WaterConstant(73u) * there + WaterConstant(77u));
            behind = !(height < world.y) ? screen : moved;
        }
    }

    // The colour maps 0 to 2 over white.
    vec4 layers = vec4(1.0);
    if (int(floatBitsToUint(WaterConstant(8u))) > 0) {
        layers = WaterFirstLayer(WaterMapColor(0u, uv, flow));
    }
    const int method1 = int(floatBitsToUint(WaterConstant(9u)));
    if (method1 > 0) {
        layers = WaterNextLayer(layers, WaterMapColor(1u, uv, flow), method1);
    }
    const int method2 = int(floatBitsToUint(WaterConstant(10u)));
    if (method2 > 0) {
        layers = WaterNextLayer(layers, WaterMapColor(2u, uv, flow), method2);
    }

    // The sun through the shadow cascade the depth falls in (words 1 and 2 their far ends).
    float shadow;
    if (WaterConstant(1u) > clip_w) {
        const float to_w = 1.0 / attr2.w;
        shadow = texture(shadow_map, vec4(attr2.x * to_w, attr2.y * to_w, 0.0, attr2.z * to_w));
    } else if (WaterConstant(2u) > clip_w) {
        const float to_w = 1.0 / attr3.w;
        shadow = texture(shadow_map, vec4(attr3.x * to_w, attr3.y * to_w, 1.0, attr3.z * to_w));
    } else {
        const float to_w = 1.0 / attr4.w;
        shadow = texture(last_shadow_map, vec3(attr4.x * to_w, attr4.y * to_w, attr4.z * to_w));
    }
    const vec3 sun = vec3(WaterConstant(56u), WaterConstant(57u), WaterConstant(58u));
    const float sun_inverse = 1.0 / sqrt(sun.z * sun.z + (sun.y * sun.y + sun.x * sun.x));
    const float sun_facing =
        -sun_inverse * (sun.z * surface.z + (sun.y * surface.y + sun.x * surface.x));
    float sum = max(0.0, sun_facing);
    vec3 lights = vec3((WaterConstant(52u) * shadow) * sum, (WaterConstant(53u) * shadow) * sum,
                       (WaterConstant(54u) * shadow) * sum);

    // The point lights of the pixel's cell of LightCompute's grid (word 63 of them at most).
    const float ndc_depth = 2.0 * depth + -1.0;
    const float view_depth = 1.0 / (WaterConstant(48u) * ndc_depth + WaterConstant(49u)) -
                             1.0 / (WaterConstant(49u) - WaterConstant(48u));
    const uint slice = uint(clamp(int(LightUint(floor(log2(view_depth)))), 0, 7));
    const uint row = LightUint(36.0 * screen.y);
    const uint column = LightUint(60.0 * screen.x);
    const uint first = 48u * (column + 60u * row + 2160u * slice);
    const uint count = floatBitsToUint(WaterConstant(63u));
    // A cell holds 48 entries, the last after its lights ~0.
    for (uint k = 0u; k < min(count, 48u); k++) {
        const uint index = CellLight(first, k);
        if (index == 0xffffffffu) {
            break;
        }
        const PointLight point = ReadPointLight(index);
        const vec3 to_light = point.position - view;
        const float distance_squared =
            to_light.z * to_light.z + (to_light.y * to_light.y + to_light.x * to_light.x);
        const float faded = PointLightFaded(point, sqrt(distance_squared));
        const float facing =
            max(0.0, (1.0 / sqrt(distance_squared)) *
                         (surface.z * to_light.z + (to_light.y * surface.y +
                                                    surface.x * to_light.x)));
        const float lit = facing * -faded + facing;
        sum = sum + lit;
        lights = lit * point.color + lights;
    }

    // The alpha: the layers' held to 0 to 1 times the vertex alpha and word 45; softened by the
    // scene behind where the flags ask, with the reflection's weight.
    float alpha = WaterConstant(45u) * (attr0.w * clamp(max(0.0, layers.w), 0.0, 1.0));
    if ((flags & WaterFlagSoft) != 0u) {
        const float soft = min(1.0, (view.z - scene) * (1.0 / WaterConstant(47u)));
        alpha = alpha * soft;
        fresnel = fresnel * soft;
    }

    // The colour times the vertex colour, its luma stepped by 256 at the light's sum and turned
    // back with its chroma, times the lights' colours (word 44); over the scene behind by the
    // alpha, under the reflection, then into the fog from word 124 to 125.
    const vec4 background =
        textureLod(sampler2D(background_texture, background_sampler), behind, 0.0);
    const float r = attr0.x * clamp(max(0.0, layers.x), 0.0, 1.0);
    const float g = attr0.y * clamp(max(0.0, layers.y), 0.0, 1.0);
    const float b = attr0.z * clamp(max(0.0, layers.z), 0.0, 1.0);
    const float luma = b * uintBitsToFloat(0x3de978d5u) +
                       (g * uintBitsToFloat(0x3f1645a2u) + uintBitsToFloat(0x3e991687u) * r);
    const float stepped = uintBitsToFloat(0x3b800000u) * floor(256.0 * (sum * luma));
    const float cb = 0.5 * b + (g * uintBitsToFloat(0xbea99ae9u) +
                                uintBitsToFloat(0xbe2cca2eu) * r);
    const float cr = b * uintBitsToFloat(0xbda685dbu) +
                     (g * uintBitsToFloat(0xbed65e89u) + 0.5 * r);
    const float scale = WaterConstant(44u);
    const float lit_g = clamp(uintBitsToFloat(0x3f36d1e1u) * -cr +
                                  (uintBitsToFloat(0x3eb0331eu) * -cb + stepped),
                              0.0, 1.0);
    const float lit_r = clamp(uintBitsToFloat(0x3fb374bcu) * cr + stepped, 0.0, 1.0);
    const float lit_b = clamp(uintBitsToFloat(0x3fe2d0e5u) * cb + stepped, 0.0, 1.0);
    const float over_g = alpha * ((scale * max(0.0, lights.y)) * lit_g + -background.y) +
                         background.y;
    const float over_r = alpha * ((scale * max(0.0, lights.x)) * lit_r + -background.x) +
                         background.x;
    const float over_b = alpha * ((scale * max(0.0, lights.z)) * lit_b + -background.z) +
                         background.z;
    const float sky_g = reflection.y * fresnel.y;
    const float sky_r = reflection.x * fresnel.x;
    const float sky_b = reflection.z * fresnel.z;
    const float under_g = sky_g * -over_g + (reflection.y * fresnel.y + over_g);
    const float under_r = sky_r * -over_r + (reflection.x * fresnel.x + over_r);
    const float under_b = sky_b * -over_b + (reflection.z * fresnel.z + over_b);
    const float near = WaterConstant(124u);
    const float to_far = 1.0 / (WaterConstant(125u) - near);
    const float fog = clamp(-near * to_far +
                                sqrt(view.z * view.z + (view.y * view.y + view.x * view.x)) *
                                    to_far,
                            0.0, 1.0);
    const float final_g = (background.y - under_g) * fog + under_g;
    const float final_r = (background.x - under_r) * fog + under_r;
    const float final_b = (background.z - under_b) * fog + under_b;
    color0 = vec4(unpackHalf2x16(packHalf2x16(vec2(final_r, final_g))),
                  unpackHalf2x16(packHalf2x16(vec2(final_b, 1.0))));
    gl_FragDepth = 1.0 - out_depth;
}
