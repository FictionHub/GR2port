// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The GPU particle pixel programs' push constants, included before particle_pixel.glsl: the draws,
// then the particles, the emitters, the effects, the descriptions and the materials. Programs that
// define PARTICLE_PUSH_LIGHTS also take the colours, the light, the fog, the instances, the point
// lights and their grid.

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#define PARTICLE_GPU_PUSH
layout(push_constant) uniform Push {
    uint64_t draws_address;
    uint64_t particle_address;
    uint64_t emitter_address;
    uint64_t effect_address;
    uint64_t description_address;
    uint64_t material_address;
#ifdef PARTICLE_PUSH_LIGHTS
    uint64_t color_address;
    uint64_t light_address;
    uint64_t fog_address;
    uint64_t instance_address;
    uint64_t point_lights_address;
    uint64_t light_grid_address;
#endif
};
