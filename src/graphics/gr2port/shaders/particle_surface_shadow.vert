// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle vertex program 0x1b50b74, a GPU particle's surface in the shadow pass: as
// model_shadow.vert, with the particle picked through the instance list where c_materialAttr.w
// reaches 1 - 2^-23, and only the colour's alpha out.
#version 460
#extension GL_GOOGLE_include_directive : require

#define PARTICLE_PUSH_LIGHTS
#include "particle_gpu_push.glsl"
#include "effect_mesh.glsl"
#include "particle_gpu_vertex.glsl"
#include "particle_gpu_model_vertex.glsl"

// The colour's alpha in x.
layout(location = 0) out vec4 attr0;

void main() {
    draw_index = uint(gl_BaseInstance) >> 20;
    gl_Position = vec4(0.0);
    EffectMesh mesh = EffectMesh(draws_address + uint64_t(draw_index) * ParticleDrawBytes + 384ul);
    if (mesh.vertices == 0ul) {
        return;
    }
    const vec3 mesh_position = MeshPosition(mesh);
    const uint instance = uint(gl_InstanceIndex) & 0xfffffu;
    uint particle = DrawWords().words[80] + instance;
    if (DrawFloat(75u) >= uintBitsToFloat(0x3f7ffffeu)) {
        particle = RecordWords(instance_address).words[(4u * particle) >> 2];
    }
    const ParticleModelPoint point = ParticleModel(particle, instance, mesh_position);
    gl_Position = VulkanClip(ModelClip(point));
    attr0 = vec4(point.color.a, 0.0, 0.0, 0.0);
}
