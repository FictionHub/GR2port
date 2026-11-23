// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle vertex program 0x1b69300, a GPU particle's ribbon, as
// particle_gpu_ribbon_vertex.glsl writes it, with the depth (z + w) / 2.
#version 460
#extension GL_GOOGLE_include_directive : require

#define PARTICLE_PUSH_LIGHTS
#include "particle_gpu_push.glsl"
#include "particle_gpu_vertex.glsl"
#include "particle_gpu_model_vertex.glsl"
#include "particle_gpu_surface_vertex.glsl"
#include "particle_gpu_sprite_vertex.glsl"
#include "particle_gpu_ribbon_vertex.glsl"

void main() {
    const vec4 clip = ParticleRibbonVertex();
    gl_Position = vec4(clip.x, clip.y, 0.5 * (clip.z + clip.w), clip.w);
}
