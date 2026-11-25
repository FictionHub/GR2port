// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's metaball vertex program 0x1b25e74: the mesh's position moved along its world normal
// by the displacement map, through World, pulled toward each neighbour and the normal bent away
// from it, then through ViewProj; for the pixel program the normal and the clip positions of the
// mesh's position through the two velocity matrices.
#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_control_flow_attributes : require

#include "metaball_vertex.glsl"

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

    // The displacement map at the texture coordinates times the scale over 1024, plus the offset.
    const vec2 place = vec2((1.0 / 1024.0) * (constants.constants[DisplacementScale] *
                                              float(texcoord.x)) +
                                constants.constants[DisplacementOffset],
                            (1.0 / 1024.0) * (constants.constants[DisplacementScale + 1u] *
                                              float(texcoord.y)) +
                                constants.constants[DisplacementOffset + 1u]);
    const float height = textureLod(sampler2D(textures[nonuniformEXT(mesh.displacement)],
                                              texture_sampler),
                                    place, 0.0)
                             .x;
    vec3 bent = TransformDirection(constants, World, normal);
    const float amount = constants.constants[DisplacementIntensity] * height;
    vec3 world = TransformPoint(constants, World, bent * amount + position);

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
    const vec3 from = TransformPoint(constants, VelocityFrom, position);
    const vec3 to = TransformPoint(constants, VelocityTo, position);
    attr0 = vec4(unit, to.x);
    attr1 = vec4(to.yz, from.xy);
    attr2 = vec4(from.z, 0.0, 0.0, 0.0);
}
