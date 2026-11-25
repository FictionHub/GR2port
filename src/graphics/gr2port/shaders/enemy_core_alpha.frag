// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's nevi core pixel program for its alpha pass, 0x1bb2be0: dithered by u_f2PunchThrough,
// the colour ramp at TexScale times one less the cosine between the view and the surface's
// normal, times GlowScale, under the colour map by its alpha, all times u_f4Alpha.x.
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

// u_f4Alpha, after the core's pixel constants.
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer CoreAlpha {
    vec4 alpha;
};
const uint64_t CoreAlphaOffset = 384ul;

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
            textureLod(sampler2D(textures[nonuniformEXT(normal_handle & TextureSlotMask)],
                                 texture_sampler),
                       texcoord, 0.0)
                .xyz;
        const vec3 surface = CoreSurface(constants, attr0, attr1.xyz, normal_map);
        const vec3 view = vec3(attr1.w, attr2.xy);
        // The cosine's scale is TexScale over both lengths.
        const float scale =
            constants.constants[TexScale] *
            (1.0 / sqrt(surface.z * surface.z + (surface.y * surface.y + surface.x * surface.x))) *
            (1.0 / sqrt(view.z * view.z + (view.y * view.y + view.x * view.x)));
        const float facing = surface.z * view.z + (view.y * surface.y + view.x * surface.x);
        const float x = clamp(-scale * facing + constants.constants[TexScale], 0.0, 1.0);
        const vec3 ramp = CoreRamp(constants, x);

        const vec4 color = texture(
            sampler2D(textures[nonuniformEXT(color_handle & TextureSlotMask)], texture_sampler),
            vec2(LowBitCleared(texcoord.x), texcoord.y));
        const float glow = constants.constants[GlowScale];
        const vec3 mixed = color.a * (-glow * ramp + color.rgb) + glow * ramp;
        const float alpha = CoreAlpha(draw + CoreAlphaOffset).alpha.x;
        const vec3 premultiplied = alpha * mixed;
        color0 = vec4(unpackHalf2x16(packHalf2x16(premultiplied.rg)),
                      unpackHalf2x16(packHalf2x16(vec2(premultiplied.b, alpha))));
        return;
    }
    discard;
}
