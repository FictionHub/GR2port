// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's effect model vertex program 0x1b30940, skinned: effect_unskinned.vert's for the
// position moved by the vertex's four bones, and the normal and tangent turned by the blended
// rows. The displacement moves the skinned position along the skinned normal.
#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "effect_matrices.glsl"
#include "effect_mesh.glsl"
#include "skin_palette.glsl"

#include "texture_slots.glsl"
layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler texture_sampler;
layout(push_constant) uniform Push {
    uint64_t draws_address;
};
layout(location = 15) flat out uint draw_index;

// The colour; the view tangent and the tangent's w; the view normal and the world position's x;
// the world position's y and z and the view position's x and y; the view position's z, texture
// coordinates 0 and 0.
layout(location = 0) out vec4 attr0;
layout(location = 1) out vec4 attr1;
layout(location = 2) out vec4 attr2;
layout(location = 3) out vec4 attr3;
layout(location = 4) out vec4 attr4;

void main() {
    draw_index = uint(gl_BaseInstance);
    const uint64_t draw = draws_address + uint64_t(draw_index) * EffectDrawBytes;
    VertexConstants constants = VertexConstants(draw);
    EffectMesh mesh = EffectMesh(draw + EffectMeshOffset);
    gl_Position = vec4(0.0);
    // A record with vertex address 0 collapses every vertex to position 0.
    if (mesh.vertices == 0ul) {
        return;
    }
    const vec3 mesh_position = MeshPosition(mesh);
    const ivec2 texcoord0 = MeshTexcoord(mesh, mesh.texcoord);
    const ivec2 texcoord1 = MeshTexcoord(mesh, mesh.texcoord1);
    const vec4 vertex_color = MeshColor(mesh);
    const vec3 mesh_normal = 2.0 * MeshNormal(mesh) + -1.0;
    const vec4 tangent = MeshTangent(mesh);
    const vec4 weights = SkinWeights(mesh.skin);
    const uvec4 bones = SkinBones(mesh.skin);
    const vec4 row0 = BlendedRow(mesh.palette, weights, bones, 0u);
    const vec4 row1 = BlendedRow(mesh.palette, weights, bones, 1u);
    const vec4 row2 = BlendedRow(mesh.palette, weights, bones, 2u);
    vec3 position = vec3(SkinnedRow(row0, mesh_position), SkinnedRow(row1, mesh_position),
                         SkinnedRow(row2, mesh_position));
    const vec3 normal = vec3(SkinnedDirection(row0, mesh_normal),
                             SkinnedDirection(row1, mesh_normal),
                             SkinnedDirection(row2, mesh_normal));
    const vec3 skinned_tangent =
        vec3(SkinnedDirection(row0, tangent.xyz), SkinnedDirection(row1, tangent.xyz),
             SkinnedDirection(row2, tangent.xyz));
    const uint flags = floatBitsToUint(constants.constants[Flags]);
    mat4 world_view;
    mat4 world_view_proj;
    EffectMatrices(constants, flags, world_view, world_view_proj);

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

    gl_Position = EffectClipPosition(constants, world_view_proj, position);
    const vec3 view_normal = UnitVector(MatrixDirection(world_view, normal));
    const vec3 view_tangent = UnitVector(MatrixDirection(world_view, skinned_tangent));
    const vec3 view_position = MatrixPoint(world_view, position);
    const vec3 world = UnitVector(TransformPoint(constants, World, position));
    attr0 = color;
    attr1 = vec4(view_tangent, tangent.w);
    attr2 = vec4(view_normal, world.x);
    attr3 = vec4(world.yz, view_position.xy);
    attr4 = vec4(view_position.z, (1.0 / 1024.0) * float(texcoord0.x),
                 (1.0 / 1024.0) * -float(texcoord0.y) + 1.0, 0.0);
}
