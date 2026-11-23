// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's particle vertex program 0x1b659e0, a GPU particle's model in the shadow pass: the
// mesh point scaled and turned by the particle, placed at the particle (or in its emitter's
// frame), through the view and projection; its colour from the material. Particles out of their
// life, dropped by the model block's level of detail, or without an emitter of kind 0 or 1 give 0.
#version 460
#extension GL_GOOGLE_include_directive : require

#define PARTICLE_PUSH_LIGHTS
#include "particle_gpu_push.glsl"
#include "effect_mesh.glsl"
#include "particle_gpu_vertex.glsl"

// The particle's colour.
layout(location = 0) out vec4 attr0;

void main() {
    draw_index = uint(gl_BaseInstance) >> 20;
    gl_Position = vec4(0.0);
    EffectMesh mesh = EffectMesh(draws_address + uint64_t(draw_index) * ParticleDrawBytes + 384ul);
    if (mesh.vertices == 0ul) {
        return;
    }
    const vec3 mesh_position = MeshPosition(mesh);
    vec4 clip = vec4(0.0);
    vec4 color = vec4(0.0);
    const uint instance = uint(gl_InstanceIndex) & 0xfffffu;
    const uint particle = DrawWords().words[80] + instance;
    const float left = ParticleFloat(particle, 7u);
    if (!(left <= 0.0)) {
        const uint particle_seed = ParticleWord(particle, 13u);
        const uint first_material = DrawWords().words[87];
        const uint seed = first_material + particle_seed;
        // Past a level of detail of 1, the instances whose hash lands half a unit off the model
        // block's mark drop out.
        bool shown = true;
        const float detail = uintBitsToFloat(ModelWords().words[0]);
        if (detail > 1.0) {
            const float hashed = float(seed) + float((instance & 0xffffu) * 256u);
            const float spread = SeedHash(GameUintOf(hashed)) * (-1.0 + detail);
            shown = !(abs(fma(uintBitsToFloat(0x3ec01c14u), -spread,
                              uintBitsToFloat(ModelWords().words[1]))) >= 0.5);
        }
        const uint emitter = ParticleWord(particle, 12u) & 0xffffffu;
        const uint kind = emitter != 0u ? EmitterWord(emitter, 10u) : 2u;
        if (shown && emitter != 0u && (kind == 0u || kind == 1u)) {
            // The particle's life from the description's shortest to longest by its hash, and
            // its age t from 0 to 1.
            const uint description = EmitterWord(emitter, 8u);
            const float shortest = DescriptionFloat(description, 39u);
            const float longest = DescriptionFloat(description, 43u);
            const float life = fma(SeedHash(particle_seed) * (longest - shortest),
                                   uintBitsToFloat(0x3ec01c14u), shortest);
            const float elapsed = life - left;
            const float t = (1.0 / life) * elapsed;
            if (!(t < 0.0) && !(t > 1.0)) {
                const uint set = EffectWord(EmitterWord(emitter, 9u), 45u) << 6;
                const uint material = first_material + set;
                // The model's scale: the material's curve 124, or a random mix of its words 24
                // to 26 and 28 to 30.
                vec3 scale;
                const float scale_random = SeedRandom(seed + 0xd80u);
                const uint scale_curve = MaterialWord(material, 124u);
                if (scale_curve != 0u) {
                    scale = SpreadCurve(scale_curve + set, t, MaterialFloat(material, 128u),
                                        scale_random).xyz;
                } else {
                    const vec3 low = MaterialVector(material, 24u).xyz;
                    const vec3 high = MaterialVector(material, 28u).xyz;
                    scale = fma(high - low, vec3(scale_random), low);
                }
                // The colour: the material's curve 125, or a random mix of its words 16 to 19
                // and 20 to 23.
                const float color_random = SeedRandom(seed + 0x4d2u);
                const uint color_curve = MaterialWord(material, 125u);
                if (color_curve != 0u) {
                    color = SpreadCurve(color_curve + set, t, MaterialFloat(material, 129u),
                                        color_random);
                } else {
                    const vec4 low = MaterialVector(material, 16u);
                    const vec4 high = MaterialVector(material, 20u);
                    color = fma(high - low, vec4(color_random), low);
                }
                // The emitter's place and scale; with the description's flag 0x100 its scale
                // moves onto the model's (w on y, word 25 on x and z).
                const vec4 emitter_place = EmitterVector(emitter, 0u);
                float place_scale = emitter_place.w;
                if ((DescriptionWord(description, 3u) & 0x100u) != 0u) {
                    const float model_scale = EmitterFloat(emitter, 25u);
                    scale.y = emitter_place.w * scale.y;
                    place_scale = 1.0;
                    scale.z = scale.z * model_scale;
                    scale.x = scale.x * model_scale;
                }
                // The particle's turn by the description's mode: 5 angles in degrees, 2 an axis
                // and angle, 3 none, else up turned onto the particle's direction.
                const uint mode = DescriptionWord(description, 49u) & 0xffu;
                const vec4 turn = ParticleVector(particle, 8u);
                vec4 q;
                if (mode != 5u) {
                    if (mode != 2u) {
                        q = vec4(0.0, 0.0, 0.0, 1.0);
                        if (mode != 3u) {
                            const float inverse = 1.0 / sqrt(fma(
                                turn.z, turn.z, fma(turn.y, turn.y, turn.x * turn.x)));
                            const float up = inverse * turn.y;
                            if (!(up >= uintBitsToFloat(0x3f7fff58u))) {
                                q.x = 1.0;
                                q.w = 0.0;
                                if (!(up <= uintBitsToFloat(0xbf7fff58u))) {
                                    const float z = inverse * turn.z;
                                    const float x = fma(-turn.x, inverse, 0.0);
                                    const float w = sqrt(0.5 * up + 0.5);
                                    const float across = fma(x, x, z * z);
                                    const float inverse_across = 1.0 / sqrt(across);
                                    const float k = sqrt(across) * (1.0 / (2.0 * w));
                                    q = vec4((inverse_across * z) * k, 0.0,
                                             (x * inverse_across) * k, w);
                                }
                            }
                        }
                    } else {
                        const float turns = uintBitsToFloat(0x3e22f983u) * (0.5 * turn.w);
                        const float s = sin(6.283185307 * fract(turns));
                        // The driver sums the axis' x and y squares first.
                        const float axis_z = sqrt(1.0 - fma(turn.x, turn.x, turn.y * turn.y));
                        q = vec4(turn.x * s, turn.y * s, s * axis_z,
                                 cos(6.283185307 * fract(abs(turns))));
                    }
                } else {
                    // Half angles; cos = sin(a + pi/2) with the add left to the driver.
                    const float k = uintBitsToFloat(0x3c0efa35u);
                    const float to_turns = uintBitsToFloat(0x3e22f983u);
                    const float quarter = uintBitsToFloat(0x3fc90fdbu);
                    const float cos_y = sin(6.283185307 * fract(to_turns * (k * turn.y + quarter)));
                    const float cos_x = sin(6.283185307 * fract(to_turns * (k * turn.x + quarter)));
                    const float sin_y = sin(6.283185307 * fract(to_turns * (k * turn.y)));
                    const float sin_x = sin(6.283185307 * fract(to_turns * (k * turn.x)));
                    const float cos_z = sin(6.283185307 * fract(to_turns * (k * turn.z + quarter)));
                    const float sin_z = sin(6.283185307 * fract(to_turns * (k * turn.z)));
                    const float cy_cx = cos_y * cos_x;
                    const float cy_sx = cos_y * sin_x;
                    const float sy_cx = sin_y * cos_x;
                    const float sy_sx = sin_y * sin_x;
                    q = vec4(fma(sy_sx, sin_z, cos_z * cy_cx), fma(sy_cx, sin_z, cos_z * cy_sx),
                             fma(cy_sx, sin_z, cos_z * sy_cx), fma(sy_sx, cos_z, sin_z * cy_cx));
                }
                // The mesh point scaled by the particle's size and the scale, turned.
                const vec4 place = ParticleVector(particle, 0u);
                const vec3 local = vec3((mesh_position.x * place.w) * scale.x,
                                        (place.w * scale.y) * mesh_position.y,
                                        (place.w * scale.z) * mesh_position.z);
                const vec3 turned = Turned(q, local);
                const bool emitter_frame = DescriptionWord(description, 18u) == 1u;
                const vec4 emitter_turn = EmitterVector(emitter, 4u);
                if ((MaterialWord(material, 136u) & 0x8u) != 0u) {
                    // The material's flag 8: the turned point is added in view space, and the
                    // depth moves.
                    vec3 position = place.xyz;
                    if (emitter_frame) {
                        position = fma(vec3(place_scale), Turned(emitter_turn, position),
                                       emitter_place.xyz);
                    }
                    const float offset = DepthOffset();
                    clip = ClipPosition(ViewPosition(position, ViewTranslation() + turned));
                    clip.z = 2.0 * offset + clip.z;
                } else {
                    // Else the turned point spins by angles in degrees: the material's curve
                    // 126, or per axis a random start in its words 32/36/40 and 33/37/41 plus a
                    // random speed in 34/38/42 and 35/39/43 times the time elapsed.
                    vec3 angles;
                    const float spin_random = SeedRandom(seed + 0x1a57u);
                    const uint spin_curve = MaterialWord(material, 126u);
                    if (spin_curve != 0u) {
                        angles = SpreadCurve(spin_curve + set, t, MaterialFloat(material, 130u),
                                             spin_random).xyz;
                    } else {
                        const vec4 spin_x = MaterialVector(material, 32u);
                        const vec4 spin_y = MaterialVector(material, 36u);
                        const vec4 spin_z = MaterialVector(material, 40u);
                        const float z_speed =
                            fma(SeedRandom(seed + 0x1a9eu), spin_z.w - spin_z.z, spin_z.z);
                        const float z_start =
                            fma(SeedRandom(seed + 0x166fu), spin_z.y - spin_z.x, spin_z.x);
                        const float y_speed =
                            fma(SeedRandom(seed + 0x12ceu), spin_y.w - spin_y.z, spin_y.z);
                        const float y_start =
                            fma(spin_y.y - spin_y.x, SeedRandom(seed + 0xe9fu), spin_y.x);
                        const float x_speed =
                            fma(SeedRandom(seed + 0x2656u), spin_x.w - spin_x.z, spin_x.z);
                        const float x_start = fma(spin_random, spin_x.y - spin_x.x, spin_x.x);
                        angles = vec3(fma(elapsed, x_speed, x_start),
                                      fma(y_speed, elapsed, y_start),
                                      fma(elapsed, z_speed, z_start));
                    }
                    // Full angles; cos = sin(a + pi/2) with the add left to the driver.
                    const float k = uintBitsToFloat(0x3c8ef921u);
                    const float to_turns = uintBitsToFloat(0x3e22f983u);
                    const float quarter = uintBitsToFloat(0x3fc90fdbu);
                    const float cos_x =
                        sin(6.283185307 * fract(to_turns * (k * angles.x + quarter)));
                    const float sin_x = sin(6.283185307 * fract(to_turns * (k * angles.x)));
                    const float cos_y =
                        sin(6.283185307 * fract(to_turns * (k * angles.y + quarter)));
                    const float sin_y = sin(6.283185307 * fract(to_turns * (k * angles.y)));
                    const float cos_z =
                        sin(6.283185307 * fract(to_turns * (k * angles.z + quarter)));
                    const float sin_z = sin(6.283185307 * fract(to_turns * (k * angles.z)));
                    // About x, then y, then z, onto the particle's place.
                    const float u = fma(cos_x, turned.z, turned.y * -sin_x);
                    const float v = fma(cos_y, turned.x, sin_y * -u);
                    const float w = fma(turned.z, sin_x, cos_x * turned.y);
                    vec3 position = vec3(fma(w, sin_z, fma(v, cos_z, place.x)),
                                         fma(-v, sin_z, fma(w, cos_z, place.y)),
                                         fma(sin_y, turned.x, fma(cos_y, u, place.z)));
                    if (emitter_frame) {
                        position = fma(vec3(place_scale), Turned(emitter_turn, position),
                                       emitter_place.xyz);
                    }
                    clip = ClipPosition(ViewPosition(position, ViewTranslation()));
                    if (DrawFloat(74u) != 0.0) {
                        const float offset = DepthOffset();
                        clip.z = 2.0 * offset + clip.z;
                    }
                }
            }
        }
    }
    gl_Position = VulkanClip(clip);
    attr0 = color;
}
