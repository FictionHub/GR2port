// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle vertex program 0x1b437cc, a GPU particle's gem in the shadow pass: as
// model_shadow.vert, with only the colour's alpha out, in the fourth output.
#version 460
#extension GL_GOOGLE_include_directive : require

#define PARTICLE_PUSH_LIGHTS
#include "particle_gpu_push.glsl"
#include "effect_mesh.glsl"
#include "particle_gpu_vertex.glsl"
#include "particle_gpu_model_vertex.glsl"

// 0 in the first three; the colour's alpha in the fourth's x.
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
    const uint instance = uint(gl_InstanceIndex) & 0xfffffu;
    const ParticleModelPoint point =
        ParticleModel(DrawWords().words[80] + instance, instance, mesh_position);
    gl_Position = VulkanClip(GemClip(point));
    attr3 = vec4(point.color.a, 0.0, 0.0, 0.0);
    attr2 = vec4(0.0);
    attr1 = vec4(0.0);
    attr0 = vec4(0.0);
}
