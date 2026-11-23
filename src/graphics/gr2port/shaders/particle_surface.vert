// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle vertex program 0x1b49bb4, a GPU particle's surface: the model's point (as
// model_shadow.vert, with the depth's sign turned in GR2port's clip z), the mesh normal turned
// as the point, the texture coordinates of the material's window, the height fog's colour and
// the colour through the colour list.
#version 460
#extension GL_GOOGLE_include_directive : require

#define PARTICLE_PUSH_LIGHTS
#include "particle_gpu_push.glsl"
#include "effect_mesh.glsl"
#include "particle_gpu_vertex.glsl"
#include "particle_gpu_model_vertex.glsl"
#include "particle_gpu_surface_vertex.glsl"

// The texture coordinates in x and y; the colour, and 1 for a shown particle in w; the normal,
// and the fog's red in w; the fog's green and blue, and the colour's alpha in z. All 0 for
// particles not shown.
layout(location = 0) out vec4 attr0;
layout(location = 1) out vec4 attr1;
layout(location = 2) out vec4 attr2;
layout(location = 3) out vec4 attr3;

void main() {
    draw_index = uint(gl_BaseInstance) >> 20;
    gl_Position = vec4(0.0);
    EffectMesh mesh = EffectMesh(draws_address + uint64_t(draw_index) * ParticleDrawBytes + 384ul);
    if (mesh.vertices == 0ul) {
        return;
    }
    const vec3 mesh_position = MeshPosition(mesh);
    const vec3 mesh_normal = MeshNormal(mesh);
    const ivec2 mesh_texcoord = MeshTexcoord(mesh, mesh.texcoord);
    const uint instance = uint(gl_InstanceIndex) & 0xfffffu;
    const ParticleModelPoint point =
        ParticleModel(DrawWords().words[80] + instance, instance, mesh_position);
    const vec4 clip = ModelClip(point);
    gl_Position = vec4(clip.x, clip.y, 0.5 * (clip.w - clip.z), clip.w);
    vec2 coordinates = vec2(0.0);
    vec3 normal = vec3(0.0);
    vec3 fog = vec3(0.0);
    vec4 color = point.color;
    float shown = 0.0;
    if (point.shown) {
        normal = ParticleNormal(point, 2.0 * mesh_normal + -1.0);
        coordinates = SurfaceCoordinates(point, mesh_texcoord);
        shown = 1.0;
        fog = HeightFog(mesh_position.y);
        color = ListedColor(point, color);
    }
    attr3 = vec4(fog.y, fog.z, color.a, 0.0);
    attr2 = vec4(normal, fog.x);
    attr1 = vec4(color.rgb, shown);
    attr0 = vec4(coordinates, 0.0, 0.0);
}
