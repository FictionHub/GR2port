// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The fog of the game's particle pixel programs for the fog passes: the volumetric fog's texture
// and the fog's constants (Scene::ParticleFog). Define PARTICLE_PUSH_LIGHTS and include
// particle_gpu_push.glsl first: the constants lie at push constant slot 8.

layout(set = 1, binding = 4) uniform texture3D fog_texture;

// The fog's constants: the depth's linear terms (0, 1), the near plane (2), one over the fog's
// reach (3), the fog's colour (4 to 6) and its strength (7), the height its density starts from
// (8), its density (9), its reach (10), the camera (12 to 14), and the matrix that takes a
// position into the fog's space (20 to 35, a column at a time).
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer FogConstants {
    float words[36];
};

// The fog to the pixel at the place: the volumetric fog's texture at the view depth's root past
// the near plane in red, green and blue, and in alpha the share that the texture and the height
// fog along the way from the camera to the position let through.
vec4 FogThrough(vec2 place, vec3 position) {
    FogConstants fog = FogConstants(fog_address);
    const float ndc = 2.0 * (1.0 - gl_FragCoord.z) + -1.0;
    const float depth = 1.0 / (fog.words[0] * ndc + fog.words[1]);
    const float slice = sqrt(clamp(fog.words[3] * max(0.0, depth - fog.words[2]), 0.0, 1.0));
    const vec4 volume =
        textureLod(sampler3D(fog_texture, curve_sampler), vec3(place, slice), 0.0);
    const float x =
        fog.words[20] * position.x +
        (fog.words[24] * position.y + (fog.words[28] * position.z + fog.words[32]));
    const float y =
        fog.words[21] * position.x +
        (fog.words[25] * position.y + (position.z * fog.words[29] + fog.words[33]));
    const float z =
        fog.words[22] * position.x +
        (fog.words[26] * position.y + (fog.words[30] * position.z + fog.words[34]));
    const float to_x = fog.words[12] - x;
    const float to_y = fog.words[13] - y;
    const float to_z = fog.words[14] - z;
    const float distance = sqrt(fma(to_z, to_z, fma(to_y, to_y, to_x * to_x)));
    // The share of the way below the height the density starts from.
    const float low = min(fog.words[13], y);
    const float inverse_span = 1.0 / (max(fog.words[13], y) - low);
    const float below = clamp(fma(low, -inverse_span, fog.words[8] * inverse_span), 0.0, 1.0);
    const float travel = min(fog.words[10], distance * below);
    return vec4(volume.rgb,
                min(volume.a, exp2(uintBitsToFloat(0x3fb8aa3bu) * (-fog.words[9] * travel))));
}

// The colour through the fog to the pixel at the place: what the fog lets through keeps the
// colour, the volumetric fog's texture adds its own, and the fog's colour covers the rest by its
// strength.
vec3 Fogged(vec3 color, vec2 place, vec3 position) {
    FogConstants fog = FogConstants(fog_address);
    const vec4 volume = FogThrough(place, position);
    const float through = volume.a;
    const float cover =
        fog.words[7] * clamp(uintBitsToFloat(0xbf317218u) * log2(through), 0.0, 1.0);
    const vec3 kept = through * color + volume.rgb;
    const vec3 fog_color = vec3(fog.words[4], fog.words[5], fog.words[6]);
    return cover * -kept + (fog_color * cover + kept);
}
