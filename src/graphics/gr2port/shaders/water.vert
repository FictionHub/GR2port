// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's water vertex program 0x1b29230: the position, moved along the normal by the
// displacement map where u_nFlags' bit 1 is set, through WorldViewProj with u_fZOffset added to
// its depth row; for the pixel program the vertex colour where bit 0 is set, the tangent and normal
// turned by WorldView to unit length, the world and view positions, the places in the three shadow
// cascades and texture coordinates 0.
#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "effect_mesh.glsl"
#include "vertex_matrices.glsl"

#include "texture_slots.glsl"
layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler texture_sampler;
layout(push_constant) uniform Push {
    uint64_t draws_address;
};
layout(location = 15) flat out uint draw_index;

// The colour; the view tangent and the tangent's w; the places in cascades 0, 1 and 2; the view
// normal and the world position's x; the world position's y and z and the view position's x and
// y; the view position's z and texture coordinates 0.
layout(location = 0) out vec4 attr0;
layout(location = 1) out vec4 attr1;
layout(location = 2) out vec4 attr2;
layout(location = 3) out vec4 attr3;
layout(location = 4) out vec4 attr4;
layout(location = 5) out vec4 attr5;
layout(location = 6) out vec4 attr6;
layout(location = 7) out vec4 attr7;

// Scene::WaterDraw: 152 words of vertex constants, 128 of pixel constants, then the mesh.
const uint64_t WaterDrawBytes = 1200ul;
const uint64_t WaterMeshOffset = 1120ul;

// The vertex constants, Buffer1 in the program's metadata: u_m4World, u_m4View, u_m4ViewI,
// u_m4WorldView, u_m4Proj, u_m4WorldViewProj, u_m4ShadowProj0 to 2, then u_f2DispUVOffset,
// u_f2DispUVRepeat, u_fZOffset, u_fDispIntensity and u_nFlags.
const uint World = 0u;
const uint WorldView = 48u;
const uint WorldViewProj = 80u;
const uint ShadowProj = 96u;
const uint DispUVOffset = 144u;
const uint DispUVRepeat = 146u;
const uint ZOffset = 148u;
const uint DispIntensity = 149u;
const uint Flags = 150u;
const uint FlagColor = 1u;
const uint FlagDisplace = 2u;

void main() {
    draw_index = uint(gl_BaseInstance);
    const uint64_t draw = draws_address + uint64_t(draw_index) * WaterDrawBytes;
    VertexConstants constants = VertexConstants(draw);
    EffectMesh mesh = EffectMesh(draw + WaterMeshOffset);
    gl_Position = vec4(0.0);
    // A record with vertex address 0 collapses every vertex to position 0.
    if (mesh.vertices == 0ul) {
        return;
    }
    vec3 position = MeshPosition(mesh);
    const ivec2 texcoord0 = MeshTexcoord(mesh, mesh.texcoord);
    const ivec2 texcoord1 = MeshTexcoord(mesh, mesh.texcoord1);
    const vec4 vertex_color = MeshColor(mesh);
    const vec3 normal = 2.0 * MeshNormal(mesh) + -1.0;
    const vec4 tangent = MeshTangent(mesh);
    const uint flags = floatBitsToUint(constants.constants[Flags]);

    if ((flags & FlagDisplace) != 0u) {
        // Texture coordinates 1 times the repeat, scaled by 1/1024, plus the offset.
        const vec2 place =
            vec2((1.0 / 1024.0) * (constants.constants[DispUVRepeat] * float(texcoord1.x)) +
                     constants.constants[DispUVOffset],
                 (1.0 / 1024.0) * (constants.constants[DispUVRepeat + 1u] * float(texcoord1.y)) +
                     constants.constants[DispUVOffset + 1u]);
        const float height =
            textureLod(sampler2D(textures[nonuniformEXT(mesh.displacement & TextureSlotMask)],
                                 texture_sampler),
                       place, 0.0)
                .x;
        position = (constants.constants[DispIntensity] * height) * normal + position;
    }
    vec4 color = vec4(1.0);
    if ((flags & FlagColor) != 0u) {
        color = clamp(max(vec4(0.0), vertex_color), 0.0, 1.0);
    }

    const float depth_translation =
        constants.constants[ZOffset] + constants.constants[WorldViewProj + 14u];
    const float clip_z =
        constants.constants[WorldViewProj + 2u] * position.x +
        (constants.constants[WorldViewProj + 6u] * position.y +
         (constants.constants[WorldViewProj + 10u] * position.z + depth_translation));
    const float clip_w = PointRow(constants, WorldViewProj, 3u, position);
    gl_Position = vec4(PointRow(constants, WorldViewProj, 0u, position),
                       PointRow(constants, WorldViewProj, 1u, position), 0.5 * (clip_w - clip_z),
                       clip_w);
    const vec3 view_normal = UnitVector(TransformDirection(constants, WorldView, normal));
    const vec3 view_tangent = UnitVector(TransformDirection(constants, WorldView, tangent.xyz));
    const vec3 view_position = TransformPoint(constants, WorldView, position);
    const vec3 world_position = TransformPoint(constants, World, position);
    attr0 = color;
    attr1 = vec4(view_tangent, tangent.w);
    attr2 = TransformPoint4(constants, ShadowProj, position);
    attr3 = TransformPoint4(constants, ShadowProj + 16u, position);
    attr4 = TransformPoint4(constants, ShadowProj + 32u, position);
    attr5 = vec4(view_normal, world_position.x);
    attr6 = vec4(world_position.yz, view_position.xy);
    attr7 = vec4(view_position.z, vec2(texcoord0) * (1.0 / 1024.0), 0.0);
}
