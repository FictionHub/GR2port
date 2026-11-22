// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the effect vertex programs share beside vertex_matrices.glsl: Scene::EffectDraw's vertex
// constants, and WorldView and WorldViewProj, taken from them or built from World, View, ViewI and
// Proj where u_nFlags turns the model to the camera.

#include "vertex_matrices.glsl"

// Scene::EffectDraw: 104 words of vertex constants, 72 of pixel constants, then the mesh.
const uint64_t EffectDrawBytes = 784ul;
const uint64_t EffectMeshOffset = 704ul;

// The vertex constants, Buffer1 in the programs' metadata: u_m4World, u_m4View, u_m4ViewI,
// u_m4WorldView, u_m4Proj, u_m4WorldViewProj, u_f2DispUVOffset, u_f2DispUVRepeat, u_fZOffset,
// u_fDispIntensity and u_nFlags.
const uint World = 0u;
const uint View = 16u;
const uint ViewI = 32u;
const uint WorldView = 48u;
const uint Proj = 64u;
const uint WorldViewProj = 80u;
const uint DispUVOffset = 96u;
const uint DispUVRepeat = 98u;
const uint ZOffset = 100u;
const uint DispIntensity = 101u;
const uint Flags = 102u;

// u_nFlags: the vertex colour, World's rotation taken in view space, World turned about its y
// axis to face the camera, and the displacement.
const uint FlagColor = 0x1u;
const uint FlagViewRotation = 0x40u;
const uint FlagFaceCamera = 0x2000u;
const uint FlagDisplace = 0x4000u;

mat4 ConstantMatrix(VertexConstants constants, uint first) {
    return mat4(constants.constants[first], constants.constants[first + 1u],
                constants.constants[first + 2u], constants.constants[first + 3u],
                constants.constants[first + 4u], constants.constants[first + 5u],
                constants.constants[first + 6u], constants.constants[first + 7u],
                constants.constants[first + 8u], constants.constants[first + 9u],
                constants.constants[first + 10u], constants.constants[first + 11u],
                constants.constants[first + 12u], constants.constants[first + 13u],
                constants.constants[first + 14u], constants.constants[first + 15u]);
}

// Row `row` of a times column `column` of b, in the order of operations of the game's programs.
float ProductElement(mat4 a, mat4 b, int row, int column) {
    return a[3][row] * b[column][3] +
           (a[2][row] * b[column][2] + (a[1][row] * b[column][1] + a[0][row] * b[column][0]));
}

mat4 Product(mat4 a, mat4 b) {
    mat4 product;
    for (int column = 0; column < 4; column++) {
        for (int row = 0; row < 4; row++) {
            product[column][row] = ProductElement(a, b, row, column);
        }
    }
    return product;
}

// World with its x and z axes turned about its y axis to face along ViewI's z axis, each axis
// keeping its length.
mat4 FaceCameraWorld(VertexConstants constants) {
    const mat4 world = ConstantMatrix(constants, World);
    const vec3 forward = UnitVector(vec3(constants.constants[ViewI + 8u],
                                         constants.constants[ViewI + 9u],
                                         constants.constants[ViewI + 10u]));
    const vec3 up = world[1].xyz;
    const vec3 across = UnitVector(vec3(-up.z * forward.y + up.y * forward.z,
                                        -up.x * forward.z + up.z * forward.x,
                                        -up.y * forward.x + up.x * forward.y));
    const vec3 side = vec3(across.y * -forward.z + forward.y * across.z,
                           across.z * -forward.x + forward.z * across.x,
                           across.x * -forward.y + forward.x * across.y);
    const vec3 lengths = vec3(
        sqrt(world[0].z * world[0].z + (world[0].y * world[0].y + world[0].x * world[0].x)),
        sqrt(world[1].z * world[1].z + (world[1].y * world[1].y + world[1].x * world[1].x)),
        sqrt(world[2].z * world[2].z + (world[2].y * world[2].y + world[2].x * world[2].x)));
    return mat4(vec4(lengths.x * across, world[0].w), vec4(side * lengths.y, world[1].w),
                vec4(lengths.z * forward, world[2].w), world[3]);
}

// WorldView and WorldViewProj as the flags pick them. With FlagViewRotation, World's rotation
// stays as it is in view space and only its translation goes through View.
void EffectMatrices(VertexConstants constants, uint flags, out mat4 world_view,
                    out mat4 world_view_proj) {
    if ((flags & FlagFaceCamera) != 0u) {
        world_view = Product(ConstantMatrix(constants, View), FaceCameraWorld(constants));
        world_view_proj = Product(ConstantMatrix(constants, Proj), world_view);
    } else if ((flags & FlagViewRotation) != 0u) {
        const mat4 world = ConstantMatrix(constants, World);
        const mat4 view = ConstantMatrix(constants, View);
        const mat4 proj = ConstantMatrix(constants, Proj);
        world_view = world;
        for (int row = 0; row < 3; row++) {
            world_view[3][row] = view[2][row] * world[3].z +
                                 (view[1][row] * world[3].y +
                                  (world[3].x * view[0][row] + view[3][row]));
        }
        for (int row = 0; row < 4; row++) {
            for (int column = 0; column < 3; column++) {
                world_view_proj[column][row] =
                    proj[2][row] * world[column].z +
                    (proj[1][row] * world[column].y + world[column].x * proj[0][row]);
            }
            world_view_proj[3][row] = proj[2][row] * world_view[3].z +
                                      (proj[1][row] * world_view[3].y +
                                       (proj[0][row] * world_view[3].x + proj[3][row]));
        }
    } else {
        world_view = ConstantMatrix(constants, WorldView);
        world_view_proj = ConstantMatrix(constants, WorldViewProj);
    }
}

// Row `row` of the matrix times the point, with 1 as its fourth component, and times the
// direction, in the order of operations of the game's programs.
float MatrixPointRow(mat4 m, int row, vec3 point) {
    return m[0][row] * point.x + (m[1][row] * point.y + (m[2][row] * point.z + m[3][row]));
}

float MatrixDirectionRow(mat4 m, int row, vec3 direction) {
    return m[0][row] * direction.x + (m[1][row] * direction.y + m[2][row] * direction.z);
}

vec3 MatrixPoint(mat4 m, vec3 point) {
    return vec3(MatrixPointRow(m, 0, point), MatrixPointRow(m, 1, point),
                MatrixPointRow(m, 2, point));
}

vec3 MatrixDirection(mat4 m, vec3 direction) {
    return vec3(MatrixDirectionRow(m, 0, direction), MatrixDirectionRow(m, 1, direction),
                MatrixDirectionRow(m, 2, direction));
}

// The clip position, u_fZOffset added to the depth row's translation, OpenGL depth (-w near to w
// far) turned to reverse depth (1 near to 0 far).
vec4 EffectClipPosition(VertexConstants constants, mat4 world_view_proj, vec3 point) {
    const float z = world_view_proj[0][2] * point.x +
                    (world_view_proj[1][2] * point.y +
                     (world_view_proj[2][2] * point.z +
                      (constants.constants[ZOffset] + world_view_proj[3][2])));
    const float w = MatrixPointRow(world_view_proj, 3, point);
    return vec4(MatrixPointRow(world_view_proj, 0, point), MatrixPointRow(world_view_proj, 1, point),
                0.5 * (w - z), w);
}
