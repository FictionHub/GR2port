// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's nevi boss body pixel program 0x1bb5710: as enemy.frag, with the surface's normal
// moved towards u_txNormalBoss's, turned by the second view tangent, by u_txMaskBoss's red times
// the vertex colour's alpha.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "dither.glsl"
#include "enemy_body_pixel.glsl"

// The view tangent and the tangent's w; the mesh's normal and the colour's alpha; the mesh's
// tangent; the second view tangent and its w; the view normal and the view position's x; the
// view position's y and z and the world normal's x and y; the world normal's z and the mesh's
// position; texture coordinates 0 and 2; texture coordinates 3 and 1.
layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 2) in vec4 attr2;
layout(location = 3) in vec4 attr3;
layout(location = 4) in vec4 attr4;
layout(location = 5) in vec4 attr5;
layout(location = 6) in vec4 attr6;
layout(location = 7) in vec4 attr7;
layout(location = 8) in vec4 attr8;
layout(location = 0) out vec4 color0;

void main() {
    const uint64_t draw = BodyDrawAddress();
    BodyConstants body = BodyConstants(draw + BodyPixelOffset);
    EnemyMesh mesh = EnemyMesh(draw + EnemyMeshOffset);
    const uint normal_handle = mesh.maps[NormalMap];
    const uint distortion_handle = mesh.maps[DistortionMap];
    const uint add_handle = mesh.maps[AddMap];
    const uint add2_handle = mesh.maps[Add2Map];
    const uint inner_handle = mesh.maps[InnerMap];
    const uint normal_boss_handle = mesh.maps[NormalBossMap];
    const uint mask_boss_handle = mesh.maps[MaskBossMap];
    const vec2 texcoord0 = attr7.xy;
    const vec2 texcoord2 = attr7.zw;
    const vec2 texcoord3 = attr8.xy;
    const vec2 texcoord1 = attr8.zw;
    // The pixel stays where u_f2PunchThrough.y reaches the game's dither noise.
    if (!(body.constants[PunchThrough + 1u] <
          DitherNoise(gl_FragCoord.xy, body.constants[PunchThrough]))) {
        const vec2 distortion = Distortion(body, distortion_handle, texcoord3);
        const InnerLayer inner = Inner(body, inner_handle, Normalized(attr1.xyz), attr6.yzw,
                                       attr2, attr1.w, distortion, texcoord0);
        const vec4 normal_map =
            textureLod(sampler2D(textures[nonuniformEXT(normal_handle)], texture_sampler),
                       Pair(body, NormalUVRepeat) * texcoord0 + Pair(body, NormalUVOffset), 0.0);
        const vec3 boss_map =
            textureLod(sampler2D(textures[nonuniformEXT(normal_boss_handle)], texture_sampler),
                       Pair(body, NormalBossUVRepeat) * texcoord1 + Pair(body, NormalBossUVOffset),
                       0.0)
                .rgb;
        const float mask =
            textureLod(sampler2D(textures[nonuniformEXT(mask_boss_handle)], texture_sampler),
                       Pair(body, MaskBossUVRepeat) *
                               (body.constants[MaskDistortionScale] * distortion + texcoord2) +
                           Pair(body, MaskBossUVOffset),
                       0.0)
                .r;
        // The surface's unit normal, moved towards the boss map's normal by the mask times the
        // vertex colour's alpha.
        const vec3 surface =
            ViewSurface(body.constants[ColorScaleNormal], normal_map.rgb, attr0, attr4.xyz);
        const float inverse = 1.0 / sqrt(surface.z * surface.z +
                                          (surface.y * surface.y + surface.x * surface.x));
        const vec3 boss_surface =
            ViewSurface(body.constants[ColorScaleNormalBoss], boss_map, attr3, attr4.xyz);
        const vec3 unit_surface = inverse * surface;
        const vec3 moved = fma(boss_surface - unit_surface, vec3(mask * attr1.w), unit_surface);
        const vec3 unit = Normalized(moved);
        vec3 color = SurfaceColor(body, Normalized(vec3(attr5.zw, attr6.x)), unit.z);
        if ((floatBitsToUint(body.constants[Flags]) & FlagLit) != 0u) {
            color = LitColor(body, color, unit, vec3(attr4.w, attr5.xy));
        }
        const AddLayer added = Added(body, add_handle, add2_handle, inner.alpha, distortion,
                                     texcoord2, texcoord3);
        // The pixel stays where its alpha reaches u_fAlphaRef.
        if (!(body.constants[AlphaRef] > added.alpha)) {
            color = FinalColor(body, color, added, inner, normal_map.a);
            color0 = vec4(unpackHalf2x16(packHalf2x16(color.rg)),
                          unpackHalf2x16(packHalf2x16(vec2(color.b, 1.0))));
            return;
        }
    }
    discard;
}
