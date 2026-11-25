// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// Scene::EffectMesh and its elements at the vertex, as the effect, water and particle model
// vertex programs fetch them.

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

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

// The game's vertex formats the elements use.
const uint MeshFormatRgb32Float = 3u;
const uint MeshFormatRgba16Float = 7u;
const uint MeshFormatRgba16Unorm = 10u;
const uint MeshFormatRg16Sint = 12u;
const uint MeshFormatRgba8Unorm = 16u;

// An element holds a byte offset in its low 8 bits and the game's vertex format above, 0 for none.
MeshWords MeshElement(EffectMesh mesh, uint element) {
    const uint stride = mesh.stride & 0xffffu;
    return MeshWords(mesh.vertices + (element & 0xffu) + uint(gl_VertexIndex) * stride);
}

vec3 MeshPosition(EffectMesh mesh) {
    MeshWords at = MeshElement(mesh, mesh.position);
    if ((mesh.position >> 8) == MeshFormatRgba16Float) {
        return vec3(unpackHalf2x16(at.words[0]), unpackHalf2x16(at.words[1]).x);
    }
    return uintBitsToFloat(uvec3(at.words[0], at.words[1], at.words[2]));
}

// 16-bit integer texture coordinates, 0 for other formats.
ivec2 MeshTexcoord(EffectMesh mesh, uint element) {
    if ((element >> 8) != MeshFormatRg16Sint) {
        return ivec2(0);
    }
    const uint word = MeshElement(mesh, element).words[0];
    return ivec2(int(word << 16) >> 16, int(word) >> 16);
}

// The vertex colour: 1 without one, else RGBA8 or RGBA16 normalized, or four half floats.
vec4 MeshColor(EffectMesh mesh) {
    const uint format = mesh.vertex_color >> 8;
    if (format == 0u) {
        return vec4(1.0);
    }
    MeshWords at = MeshElement(mesh, mesh.vertex_color);
    if (format == MeshFormatRgba8Unorm) {
        return unpackUnorm4x8(at.words[0]);
    }
    if (format == MeshFormatRgba16Unorm) {
        return vec4(unpackUnorm2x16(at.words[0]), unpackUnorm2x16(at.words[1]));
    }
    return vec4(unpackHalf2x16(at.words[0]), unpackHalf2x16(at.words[1]));
}

// The normal from 0 to 1: three small floats in a word, each turned into a half float. A mesh
// without normals gives 0.5.
vec3 MeshNormal(EffectMesh mesh) {
    if ((mesh.normal >> 8) == 0u) {
        return vec3(0.5);
    }
    const uint word = MeshElement(mesh, mesh.normal).words[0];
    return vec3(unpackHalf2x16((word << 4) & 0x7ff0u).x,
                unpackHalf2x16((word >> 7) & 0x7ff0u).x,
                unpackHalf2x16((word >> 17) & 0x7fe0u).x);
}

// The tangent as four half floats, 0 without one.
vec4 MeshTangent(EffectMesh mesh) {
    if ((mesh.tangent >> 8) == 0u) {
        return vec4(0.0);
    }
    MeshWords at = MeshElement(mesh, mesh.tangent);
    return vec4(unpackHalf2x16(at.words[0]), unpackHalf2x16(at.words[1]));
}
