// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle vertex program 0x1b4cfec, a GPU particle's surface with a tangent: as
// particle_surface.vert, with the mesh's tangent turned as the normal is and the parallax shift
// of the moved view position.
#version 460
#extension GL_GOOGLE_include_directive : require

#define PARTICLE_PUSH_LIGHTS
#include "particle_gpu_push.glsl"
#include "effect_mesh.glsl"
#include "particle_gpu_vertex.glsl"
#include "particle_gpu_model_vertex.glsl"
#include "particle_gpu_surface_vertex.glsl"

// The tangent, and its w; the texture coordinates in x and y, and the parallax shift in z and w;
// the colour, and 1 in w; the normal, and the fog's red in w; the fog's green and blue, and the
// colour's alpha in z. All 0 for particles not shown.
layout(location = 0) out vec4 attr0;
layout(location = 1) out vec4 attr1;
layout(location = 2) out vec4 attr2;
layout(location = 3) out vec4 attr3;
layout(location = 4) out vec4 attr4;

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
    const vec4 mesh_tangent = MeshTangent(mesh);
    const uint instance = uint(gl_InstanceIndex) & 0xfffffu;
    const ParticleModelPoint point =
        ParticleModel(DrawWords().words[80] + instance, instance, mesh_position);
    const vec4 clip = ModelClip(point);
    gl_Position = vec4(clip.x, clip.y, 0.5 * (clip.w - clip.z), clip.w);
    vec2 coordinates = vec2(0.0);
    vec2 parallax = vec2(0.0);
    vec3 normal = vec3(0.0);
    vec4 tangent = vec4(0.0);
    vec3 fog = vec3(0.0);
    vec4 color = point.color;
    float shown = 0.0;
    if (point.shown) {
        normal = ParticleNormal(point, 2.0 * mesh_normal + -1.0);
        tangent = vec4(ParticleNormal(point, mesh_tangent.xyz), mesh_tangent.w);
        coordinates = MapCoordinates(point, 0u, mesh_texcoord);
        parallax = ParallaxShift(MovedView(point), normal, tangent);
        shown = 1.0;
        fog = HeightFog(mesh_position.y);
        color = ListedColor(point.emitter, color);
    }
    attr4 = vec4(fog.y, fog.z, color.a, 0.0);
    attr3 = vec4(normal, fog.x);
    attr2 = vec4(color.rgb, shown);
    attr1 = vec4(coordinates, parallax);
    attr0 = tangent;
}
