// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle vertex program 0x1b5da3c in the shadow pass, a GPU particle's effect mesh,
// as particle_gpu_effect_vertex.glsl writes it, with the depth (z + w) / 2.
#version 460
#extension GL_GOOGLE_include_directive : require

#define PARTICLE_MODEL_SIZED_MESH
#define PARTICLE_PUSH_LIGHTS
#include "particle_gpu_push.glsl"
#include "effect_mesh.glsl"
#include "particle_gpu_vertex.glsl"
#include "particle_gpu_model_vertex.glsl"
#include "particle_gpu_surface_vertex.glsl"
#include "particle_gpu_effect_vertex.glsl"

void main() {
    gl_Position = VulkanClip(ParticleEffectVertex());
}
