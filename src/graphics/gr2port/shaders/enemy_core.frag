// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's nevi core pixel program 0x1bb2178: dithered by u_f2PunchThrough, the colour ramp at
// TexScale times one less the cosine between the view and the surface's normal, times GlowScale,
// under the colour map by its alpha, plus the light's colour times the specular highlight of
// u_f3LightWorldDir.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "dither.glsl"
#include "enemy_core_pixel.glsl"

// The tangent and its sign; the world normal and the view direction's x; the view direction's y
// and z, and the texture coordinates.
layout(location = 0) in vec4 attr0;
layout(location = 1) in vec4 attr1;
layout(location = 2) in vec4 attr2;
layout(location = 0) out vec4 color0;

// u_fSpecularScale, u_fSpecularPower, u_f3LightWorldDir and u_f3LightColor.
const uint SpecularScale = 15u;
const uint SpecularPower = 16u;
const uint LightWorldDir = 17u;
const uint LightColor = 20u;

float LowBitCleared(float x) {
    return uintBitsToFloat(floatBitsToUint(x) & ~1u);
}

void main() {
    const uint64_t draw = CoreDrawAddress();
    CoreConstants constants = CoreConstants(draw + CorePixelOffset);
    // The maps' handles are read before the dither test, as in the game's program.
    EnemyMesh mesh = EnemyMesh(draw + EnemyMeshOffset);
    const uint color_handle = mesh.maps[ColorMap];
    const uint normal_handle = mesh.maps[NormalMap];
    // The pixel stays where u_f2PunchThrough.y reaches the game's dither noise.
    if (!(DitherNoise(gl_FragCoord.xy, constants.constants[PunchThrough]) >
          constants.constants[PunchThrough + 1u])) {
        const vec2 texcoord = attr2.zw;
        const vec3 normal_map =
            textureLod(sampler2D(textures[nonuniformEXT(normal_handle)], texture_sampler),
                       texcoord, 0.0)
                .xyz;
        const vec3 surface = Normalized(CoreSurface(constants, attr0, attr1.xyz, normal_map));
        const vec3 view = Normalized(vec3(attr1.w, attr2.xy));
        const float tex_scale = constants.constants[TexScale];
        const float cosine = surface.z * view.z + (surface.y * view.y + view.x * surface.x);
        const vec3 ramp = CoreRamp(constants, clamp(-tex_scale * cosine + tex_scale, 0.0, 1.0));

        const vec4 color =
            texture(sampler2D(textures[nonuniformEXT(color_handle)], texture_sampler),
                    vec2(LowBitCleared(texcoord.x), texcoord.y));
        // The light reflected about the surface's normal against the view, in the game's order.
        const vec3 light = CoreColor(constants, LightWorldDir);
        const float twice =
            (light.z * surface.z + (light.y * surface.y + light.x * surface.x)) * 2.0;
        float reflected = light.x * view.x;
        reflected = twice * (-view.x * surface.x) + reflected;
        reflected = light.y * view.y + reflected;
        reflected = twice * (-view.y * surface.y) + reflected;
        reflected = light.z * view.z + reflected;
        reflected = twice * (-view.z * surface.z) + reflected;
        const float specular =
            constants.constants[SpecularScale] *
            exp2(constants.constants[SpecularPower] * log2(max(0.0, reflected)));
        // The colour map's share over the glow, rounded as GR2port's earlier translation of the
        // game's program rounds it: red's in one fused step, green's and blue's from the rounded
        // glow.
        const float glow = constants.constants[GlowScale];
        const vec3 glowing = glow * ramp;
        const vec3 share = vec3(fma(-glow, ramp.r, color.r), color.gb - glowing.gb);
        const vec3 mixed = fma(vec3(color.a), share, glowing);
        const vec3 lit = clamp(CoreColor(constants, LightColor) * specular + mixed, 0.0, 1.0);
        color0 = vec4(unpackHalf2x16(packHalf2x16(lit.rg)),
                      unpackHalf2x16(packHalf2x16(vec2(lit.b, 1.0))));
        return;
    }
    discard;
}
