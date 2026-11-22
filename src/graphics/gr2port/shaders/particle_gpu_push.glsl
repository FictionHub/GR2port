// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The GPU particle pixel programs' push constants, included before particle_pixel.glsl: the draws,
// then the particles, the emitters, the effects, the descriptions and the materials.

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#define PARTICLE_GPU_PUSH
layout(push_constant) uniform Push {
    uint64_t draws_address;
    uint64_t particle_address;
    uint64_t emitter_address;
    uint64_t effect_address;
    uint64_t description_address;
    uint64_t material_address;
};
