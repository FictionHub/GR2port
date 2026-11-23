// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's nevi body pixel program 0x1bb3618: dithered by u_f2PunchThrough; an inner layer seen
// through the surface by parallax where the camera faces it within u_fClipAngle; the normal map's
// mix of the ambient light and u_f3FresnelColor at its edges with u_f3BaseColor; the directional
// and point lights when u_nFlags says so; the two add maps; the alpha test; and the colour seen
// through the body by u_fSelfTranslucency.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "dither.glsl"
#include "enemy_body_pixel.glsl"

// The view tangent and the tangent's w; the mesh's normal and the colour's alpha; the mesh's
// tangent; the view normal and the view position's x; the view position's y and z and the world
// normal's x and y; the world normal's z and the mesh's position; texture coordinates 0 and 2;
// texture coordinates 3.
layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 2) in vec4 attr2;
layout(location = 3) in vec4 attr3;
layout(location = 4) in vec4 attr4;
layout(location = 5) in vec4 attr5;
layout(location = 6) in vec4 attr6;
layout(location = 7) in vec4 attr7;
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
    const vec2 texcoord0 = attr6.xy;
    const vec2 texcoord2 = attr6.zw;
    const vec2 texcoord3 = attr7.xy;
    // The pixel stays where u_f2PunchThrough.y reaches the game's dither noise.
    if (!(body.constants[PunchThrough + 1u] <
          DitherNoise(gl_FragCoord.xy, body.constants[PunchThrough]))) {
        const vec2 distortion = Distortion(body, distortion_handle, texcoord3);
        const InnerLayer inner = Inner(body, inner_handle, Normalized(attr1.xyz), attr5.yzw,
                                       attr2, attr1.w, distortion, texcoord0);
        const vec4 normal_map =
            textureLod(sampler2D(textures[nonuniformEXT(normal_handle)], texture_sampler),
                       Pair(body, NormalUVRepeat) * texcoord0 + Pair(body, NormalUVOffset), 0.0);
        const vec3 surface = Normalized(
            ViewSurface(body.constants[ColorScaleNormal], normal_map.rgb, attr0, attr3.xyz));
        vec3 color = SurfaceColor(body, Normalized(vec3(attr4.zw, attr5.x)), surface.z);
        if ((floatBitsToUint(body.constants[Flags]) & FlagLit) != 0u) {
            color = LitColor(body, color, surface, vec3(attr3.w, attr4.xy));
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
