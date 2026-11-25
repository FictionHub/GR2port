// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the metaball vertex programs read: Scene::MetaballDraw, the game's vertex constants with
// the neighbours its draw writes, the pixel constants and then the mesh, the mesh's elements at
// the vertex, and the texture table that holds the displacement map.

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

#include "texture_slots.glsl"
layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler texture_sampler;

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer MetaballConstants {
    float constants[];
};
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer MeshWords {
    uint words[];
};
layout(buffer_reference, std430, buffer_reference_align = 8) readonly buffer EffectMesh {
    uint64_t vertices;
    uint64_t skin;
    uint64_t palette;
    uint stride;
    uint position;
    uint texcoord;
    uint texcoord1;
    uint normal;
    uint tangent;
    uint vertex_color;
    uint displacement;
    uint color_maps[5];
    uint reflection;
};
layout(push_constant) uniform Push {
    uint64_t draws_address;
};
const uint64_t MetaballDrawBytes = 512ul;
const uint64_t MetaballMeshOffset = 432ul;

// The vertex constants: ViewProj, World, the two matrices whose clip positions the game's pixel
// program turns into velocity, the displacement map's offset and scale, the neighbours (centre
// and 1 / (radius + 4) each), their count and the displacement's intensity.
const uint ViewProj = 0u;
const uint World = 16u;
const uint VelocityFrom = 32u;
const uint VelocityTo = 48u;
const uint DisplacementOffset = 64u;
const uint DisplacementScale = 66u;
const uint Neighbours = 68u;
const uint NeighbourCount = 92u;
const uint DisplacementIntensity = 93u;

// The game's vertex formats the mesh's elements use here.
const uint FormatRgba16Float = 7u;
const uint FormatRg16Sint = 12u;

layout(location = 15) flat out uint draw_index;

// The draw's record. The instance names it, and the pixel program reads the same record.
uint64_t DrawAddress() {
    draw_index = uint(gl_BaseInstance);
    return draws_address + uint64_t(draw_index) * MetaballDrawBytes;
}

// An element holds a byte offset in its low 8 bits and the game's vertex format above, 0 for none.
MeshWords Element(EffectMesh mesh, uint element) {
    const uint stride = mesh.stride & 0xffffu;
    return MeshWords(mesh.vertices + uint64_t(gl_VertexIndex) * stride + (element & 0xffu));
}

vec3 MeshPosition(EffectMesh mesh) {
    MeshWords at = Element(mesh, mesh.position);
    if ((mesh.position >> 8) == FormatRgba16Float) {
        return vec3(unpackHalf2x16(at.words[0]), unpackHalf2x16(at.words[1]).x);
    }
    return uintBitsToFloat(uvec3(at.words[0], at.words[1], at.words[2]));
}

// The normal from 0 to 1: three small floats in a word, each turned into a half float. A mesh
// without normals gives 0.5.
vec3 MeshNormal(EffectMesh mesh) {
    if ((mesh.normal >> 8) == 0u) {
        return vec3(0.5);
    }
    const uint word = Element(mesh, mesh.normal).words[0];
    return vec3(unpackHalf2x16((word << 4) & 0x7ff0u).x,
                unpackHalf2x16((word >> 7) & 0x7ff0u).x,
                unpackHalf2x16((word >> 17) & 0x7fe0u).x);
}

// 16-bit integer texture coordinates, 0 for other formats.
ivec2 MeshTexcoord(EffectMesh mesh, uint element) {
    if ((element >> 8) != FormatRg16Sint) {
        return ivec2(0);
    }
    const uint word = Element(mesh, element).words[0];
    return ivec2(int(word << 16) >> 16, int(word) >> 16);
}

// Row `row` of the column-major 4x4 matrix at constant `first` times the point, with 1 as its
// fourth component, in the order of operations of the game's programs.
float PointRow(MetaballConstants constants, uint first, uint row, vec3 point) {
    const uint at = first + row;
    return constants.constants[at] * point.x +
           (constants.constants[at + 4u] * point.y +
            (constants.constants[at + 8u] * point.z + constants.constants[at + 12u]));
}

vec3 TransformPoint(MetaballConstants constants, uint first, vec3 point) {
    return vec3(PointRow(constants, first, 0u, point), PointRow(constants, first, 1u, point),
                PointRow(constants, first, 2u, point));
}

// Row `row` of the matrix's upper 3x3 times the direction.
float DirectionRow(MetaballConstants constants, uint first, uint row, vec3 direction) {
    const uint at = first + row;
    return constants.constants[at] * direction.x +
           (constants.constants[at + 4u] * direction.y +
            constants.constants[at + 8u] * direction.z);
}

vec3 TransformDirection(MetaballConstants constants, uint first, vec3 direction) {
    return vec3(DirectionRow(constants, first, 0u, direction),
                DirectionRow(constants, first, 1u, direction),
                DirectionRow(constants, first, 2u, direction));
}

// The clip position, OpenGL depth (-w near to w far) turned to reverse depth (1 near to 0 far).
vec4 ClipPosition(MetaballConstants constants, uint first, vec3 point) {
    const float z = PointRow(constants, first, 2u, point);
    const float w = PointRow(constants, first, 3u, point);
    return vec4(PointRow(constants, first, 0u, point), PointRow(constants, first, 1u, point),
                0.5 * (w - z), w);
}
