// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle vertex program 0x1b3bc60, a GPU particle's model: the model's point with
// the colour times the vertex colour, the mesh's normal and tangent turned as the point turns,
// the moved view position and the texture coordinates of the material's four map windows.
#version 460
#extension GL_GOOGLE_include_directive : require

#define PARTICLE_PUSH_LIGHTS
#include "particle_gpu_push.glsl"
#include "effect_mesh.glsl"
#include "particle_gpu_vertex.glsl"
#include "particle_gpu_model_vertex.glsl"
#include "particle_gpu_surface_vertex.glsl"

// The colour; the distance (the view position's -z) in x and the particle's size in w; maps 0
// to 3's texture coordinates; the normal, and the age t in w; the tangent, and its w; the view
// position, and 1; the particle's effect, emitter, description and index, as bits; the material
// set twice and the set plus 0x8000, as bits. All 0 for particles not shown.
layout(location = 0) out vec4 attr0;
layout(location = 1) out vec4 attr1;
layout(location = 2) out vec4 attr2;
layout(location = 3) out vec4 attr3;
layout(location = 4) out vec4 attr4;
layout(location = 5) out vec4 attr5;
layout(location = 6) out vec4 attr6;
layout(location = 7) out vec4 attr7;
layout(location = 8) out vec4 attr8;
layout(location = 9) out vec4 attr9;
layout(location = 10) out vec4 attr10;

void main() {
    draw_index = uint(gl_BaseInstance) >> 20;
    gl_Position = vec4(0.0);
    EffectMesh mesh = EffectMesh(draws_address + uint64_t(draw_index) * ParticleDrawBytes + 384ul);
    if (mesh.vertices == 0ul) {
        return;
    }
    const vec4 mesh_color = MeshColor(mesh);
    const vec3 mesh_position = MeshPosition(mesh);
    const vec3 mesh_normal = MeshNormal(mesh);
    const vec4 mesh_tangent = MeshTangent(mesh);
    const ivec2 mesh_texcoord = MeshTexcoord(mesh, mesh.texcoord);
    const uint instance = uint(gl_InstanceIndex) & 0xfffffu;
    const uint particle = DrawWords().words[80] + instance;
    const ParticleModelPoint point = ParticleModel(particle, instance, mesh_position);
    const vec4 clip = ModelClip(point);
    gl_Position = vec4(clip.x, clip.y, 0.5 * (clip.w - clip.z), clip.w);
    vec4 color = vec4(0.0);
    vec3 view = vec3(0.0);
    float distance = 0.0;
    float size = 0.0;
    vec2 map0 = vec2(0.0);
    vec2 map1 = vec2(0.0);
    vec2 map2 = vec2(0.0);
    vec2 map3 = vec2(0.0);
    vec4 normal = vec4(0.0);
    vec4 tangent = vec4(0.0);
    float shown = 0.0;
    uvec4 records = uvec4(0u);
    uvec4 set_bits = uvec4(0u);
    if (point.shown) {
        view = MovedView(point);
        // The sign bit flipped as an integer, which keeps a NaN's sign as the game's program does.
        // The driver turns an xor of the sign bit into a float negate; a bit insert stays.
        const uint z = floatBitsToUint(view.z);
        distance = uintBitsToFloat(bitfieldInsert(z, ~(z >> 31), 31, 1));
        size = point.size;
        normal = vec4(ParticleNormal(point, 2.0 * mesh_normal + -1.0), point.t);
        tangent = vec4(ParticleNormal(point, mesh_tangent.xyz), mesh_tangent.w);
        map0 = MapCoordinates(point, 0u, mesh_texcoord);
        map1 = MapCoordinates(point, 1u, mesh_texcoord);
        map2 = MapCoordinates(point, 2u, mesh_texcoord);
        map3 = MapCoordinates(point, 3u, mesh_texcoord);
        color = ListedColor(point, mesh_color * point.color);
        shown = 1.0;
        records = uvec4(EmitterWord(point.emitter, 9u), point.emitter,
                        EmitterWord(point.emitter, 8u), particle);
        const uint set = point.material - DrawWords().words[87];
        set_bits = uvec4(set, set, set + 0x8000u, 0u);
    }
    attr10 = uintBitsToFloat(set_bits);
    attr9 = uintBitsToFloat(records);
    attr8 = vec4(view, shown);
    attr7 = tangent;
    attr6 = normal;
    attr5 = vec4(map3, 0.0, 0.0);
    attr4 = vec4(map2, 0.0, 0.0);
    attr3 = vec4(map1, 0.0, 0.0);
    attr2 = vec4(map0, 0.0, 0.0);
    attr1 = vec4(distance, 0.0, 0.0, size);
    attr0 = color;
}
