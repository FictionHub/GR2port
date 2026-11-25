// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's skinned metaball vertex program 0x1b265d0: metaball.vert's for the position moved by
// the vertex's four bones and the normal turned by the blended rows. The velocity matrices take
// the skinned position before the displacement.
#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_control_flow_attributes : require

#include "metaball_vertex.glsl"
#include "skin_palette.glsl"

// The normal and the first row through VelocityTo; that matrix's other two rows and two rows
// through VelocityFrom; its third row.
layout(location = 0) out vec4 attr0;
layout(location = 1) out vec4 attr1;
layout(location = 2) out vec4 attr2;

void main() {
    const uint64_t draw = DrawAddress();
    MetaballConstants constants = MetaballConstants(draw);
    MeshWords words = MeshWords(draw);
    EffectMesh mesh = EffectMesh(draw + MetaballMeshOffset);
    gl_Position = vec4(0.0);
    // A record with vertex address 0 collapses every vertex to position 0.
    if (mesh.vertices == 0ul) {
        return;
    }
    const vec3 position = MeshPosition(mesh);
    const vec3 normal = 2.0 * MeshNormal(mesh) + -1.0;
    const ivec2 texcoord = MeshTexcoord(mesh, mesh.texcoord);
    const vec4 weights = SkinWeights(mesh.skin);
    const uvec4 bones = SkinBones(mesh.skin);
    const vec4 row0 = BlendedRow(mesh.palette, mesh.stride >> 16, weights, bones, 0u);
    const vec4 row1 = BlendedRow(mesh.palette, mesh.stride >> 16, weights, bones, 1u);
    const vec4 row2 = BlendedRow(mesh.palette, mesh.stride >> 16, weights, bones, 2u);
    const vec3 skinned =
        vec3(SkinnedRow(row0, position), SkinnedRow(row1, position), SkinnedRow(row2, position));
    const vec3 skinned_normal = vec3(SkinnedDirection(row0, normal), SkinnedDirection(row1, normal),
                                     SkinnedDirection(row2, normal));

    // The displacement map at the texture coordinates times the scale over 1024, plus the offset.
    const vec2 place = vec2((1.0 / 1024.0) * (constants.constants[DisplacementScale] *
                                              float(texcoord.x)) +
                                constants.constants[DisplacementOffset],
                            (1.0 / 1024.0) * (constants.constants[DisplacementScale + 1u] *
                                              float(texcoord.y)) +
                                constants.constants[DisplacementOffset + 1u]);
    const float height =
        textureLod(sampler2D(textures[nonuniformEXT(mesh.displacement & TextureSlotMask)],
                             texture_sampler),
                   place, 0.0)
            .x;
    vec3 bent = TransformDirection(constants, World, skinned_normal);
    const float amount = constants.constants[DisplacementIntensity] * height;
    vec3 world = TransformPoint(constants, World, bent * amount + skinned);

    // Each neighbour pulls the position toward its centre and bends the normal away, by 1 less
    // the distance over its radius plus 4. The loop stays rolled, as in the game's program.
    const uint neighbours = words.words[NeighbourCount];
    // The record's 96 words hold at most seven.
    [[dont_unroll]] for (uint i = 0u; i < min(neighbours, 7u); i++) {
        const uint at = Neighbours + 4u * i;
        const vec3 toward = vec3(constants.constants[at], constants.constants[at + 1u],
                                 constants.constants[at + 2u]) -
                            world;
        const float distance2 = toward.z * toward.z + (toward.y * toward.y + toward.x * toward.x);
        const float pull = clamp(-constants.constants[at + 3u] * sqrt(distance2) + 1.0, 0.0, 1.0);
        const float bend = pull * (1.0 / sqrt(distance2));
        bent = toward * -bend + bent;
        world = toward * pull + world;
    }
    const vec3 unit = (1.0 / sqrt(bent.z * bent.z + (bent.y * bent.y + bent.x * bent.x))) * bent;

    gl_Position = ClipPosition(constants, ViewProj, world);
    const vec3 from = TransformPoint(constants, VelocityFrom, skinned);
    const vec3 to = TransformPoint(constants, VelocityTo, skinned);
    attr0 = vec4(unit, to.x);
    attr1 = vec4(to.yz, from.xy);
    attr2 = vec4(from.z, 0.0, 0.0, 0.0);
}
