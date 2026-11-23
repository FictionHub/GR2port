// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The GPU particle sprite vertex program (0x1b37a5c, in both passes): a quad of four corners per
// particle in view space, turned by its billboard mode's angle or stretched between two points,
// with the texture coordinates of the material's four map windows at its corners. Include
// particle_gpu_surface_vertex.glsl first.

// The colour; the distance (the view position's -z) in x, the angle in y and the width in w;
// maps 0 to 3's texture coordinates, map 0's with the corner's sides (-1 or 1) in z and w and
// map 1's with the length of the particle's turn in z; 1 in y and the age t in w; 1 in x; the
// corner's view position, and 1; the particle's effect, emitter, description and index, as bits;
// the material set twice and the set plus 0x8000, as bits. All 0 for corners not shown.
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

// The sign bit flipped as an integer, as the game's program does: a NaN keeps its sign. (The
// driver turns an xor of the sign bit into a float negate; a bit insert stays.)
float FlippedSign(float x) {
    const uint bits = floatBitsToUint(x);
    return uintBitsToFloat(bitfieldInsert(bits, ~(bits >> 31), 31, 1));
}

// The game's atan2 of a unit vector's x over its y: an odd polynomial in the smaller over the
// larger magnitude, mirrored about pi/4 and below the x axis, then given the sign.
float GameAtan2(float x, float y) {
    const float ratio = (1.0 / max(abs(x), abs(y))) * min(abs(x), abs(y));
    const float square = ratio * ratio;
    float p = uintBitsToFloat(0x3caaae29u) * square + uintBitsToFloat(0xbdae5a36u);
    p = p * square + uintBitsToFloat(0x3e3876e2u);
    p = p * square + uintBitsToFloat(0xbea91cf3u);
    p = p * square + uintBitsToFloat(0x3f7ff738u);
    const float steep = abs(y) < abs(x) ? -2.0 * (ratio * p) + uintBitsToFloat(0x3fc90fd8u) : 0.0;
    const float below = y < -y ? uintBitsToFloat(0xc0490fdcu) : 0.0;
    const float angle = p * ratio + (below + steep);
    return min(x, y) < -min(x, y) && max(x, y) >= -max(x, y) ? FlippedSign(angle) : angle;
}

// The screen direction from one view position's x and y to another's, unit length, and the
// length before.
vec3 ScreenDirection(vec2 from, vec2 to) {
    const vec2 d = to - from;
    const float square = d.y * d.y + d.x * d.x;
    const float inverse = 1.0 / sqrt(square);
    return vec3(inverse * d.x, inverse * d.y, square);
}

// A row of c_viewMat applied to a target point (x, y, z, w): x, y and z, then the translation
// times w.
float TargetRow(uint word, vec4 p) {
    return fma(DrawFloat(word + 3u), p.w,
               fma(DrawFloat(word + 2u), p.z,
                   fma(DrawFloat(word + 1u), p.y, DrawFloat(word) * p.x)));
}

// The quaternion's turn of up (0, 1, 0), in the order of steps of the euler mode.
vec3 EulerUp(vec4 q) {
    const float minus_2z = -q.z + -q.z;
    return vec3(q.x * q.y + (q.w * minus_2z + q.y * q.x),
                q.y * q.y + (q.w * q.w + (q.z * -q.z + -q.x * q.x)),
                q.z * q.y + (q.x * q.w + (q.y * q.z + q.x * q.w)));
}

// The quaternion's turn of up (0, 1, 0), in the order of steps of the axis and angle mode, whose
// z is s times the axis' z.
vec3 AxisUp(vec4 q, float s, float axis_z) {
    const float minus_z = -s * axis_z + 0.0;
    const float minus_2z = -s * axis_z + minus_z;
    return vec3(q.y * q.x + (minus_2z * q.w + q.y * q.x),
                q.y * q.y + (q.w * q.w + (q.z * minus_z + -q.x * q.x)),
                q.y * q.z + (q.x * q.w + (minus_z * -q.y + q.w * q.x)));
}

// The texture coordinates of a window's corner (u from x or z, v from y or w), turned about the
// window's centre by the angle where the map's flag 3 turns a scrolled window.
vec2 SpriteCorner(vec4 window, bool far_u, bool far_v, bool turn, float angle) {
    vec2 corner = vec2(far_u ? window.z : window.x, far_v ? window.w : window.y);
    if (turn) {
        const float cosine = sin(6.283185307 *
                                 fract(fma(angle, uintBitsToFloat(0x3e22f983u), 0.25)));
        const float sine = sin(6.283185307 * fract(uintBitsToFloat(0x3e22f983u) * angle));
        const vec2 sum = vec2(window.x + window.z, window.y + window.w);
        const vec2 d = -0.5 * sum + corner;
        corner = vec2(-d.y * sine + (cosine * d.x + 0.5 * sum.x),
                      d.y * cosine + (sine * d.x + 0.5 * sum.y));
    }
    return corner;
}

// a - b - c in that order, which the driver would otherwise regroup.
float Difference(float a, float b, float c) {
    precise const float d = a - b;
    precise const float e = d - c;
    return e;
}

// One corner's texture coordinates of the four maps.
vec2[4] SpriteCorners(vec4 windows[4], bool turned[4], bool far_u, bool far_v, float angle) {
    return vec2[4](SpriteCorner(windows[0], far_u, far_v, turned[0], angle),
                   SpriteCorner(windows[1], far_u, far_v, turned[1], angle),
                   SpriteCorner(windows[2], far_u, far_v, turned[2], angle),
                   SpriteCorner(windows[3], far_u, far_v, turned[3], angle));
}

// Writes the outputs and gives the clip position.
vec4 ParticleSpriteVertex() {
    draw_index = uint(gl_BaseInstance) >> 20;
    vec4 color = vec4(0.0);
    float distance = 0.0;
    float angle = 0.0;
    float width = 0.0;
    vec4 map0 = vec4(0.0);
    vec3 map1 = vec3(0.0);
    vec2 map2 = vec2(0.0);
    vec2 map3 = vec2(0.0);
    float shown = 0.0;
    float t = 0.0;
    vec3 view = vec3(0.0);
    uvec4 records = uvec4(0u);
    uvec4 set_bits = uvec4(0u);
    vec4 clip = vec4(0.0);
    const uint corner = uint(gl_VertexIndex);
    const uint instance = uint(gl_InstanceIndex) & 0xfffffu;
    const uint particle = DrawWords().words[80] + instance;
    const vec4 motion = ParticleVector(particle, 4u);
    const uint emitter = ParticleWord(particle, 12u) & 0xffffffu;
    const uint kind = emitter != 0u ? EmitterWord(emitter, 10u) : 2u;
    if (corner < 4u && !(motion.w <= 0.0) && emitter != 0u && (kind == 0u || kind == 1u)) {
        const uint particle_seed = ParticleWord(particle, 13u);
        const uint description = EmitterWord(emitter, 8u);
        const float shortest = DescriptionFloat(description, 39u);
        const float longest = DescriptionFloat(description, 43u);
        const float life = fma(SeedHash(particle_seed) * (longest - shortest),
                               uintBitsToFloat(0x3ec01c14u), shortest);
        const float elapsed = life - motion.w;
        const float age = (1.0 / life) * elapsed;
        if (!(age < 0.0) && !(age > 1.0)) {
            t = age;
            const uint effect = EmitterWord(emitter, 9u);
            const uint seed = DrawWords().words[87] + particle_seed;
            const uint set = EffectWord(effect, 45u) << 6;
            const uint material = DrawWords().words[87] + set;
            // The sprite's scale: the material's curve 124, or a random mix of its words 24 and 25
            // and 28 and 29.
            vec2 scale;
            const float scale_random = SeedRandom(seed + 0xd80u);
            const uint scale_curve = MaterialWord(material, 124u);
            if (scale_curve != 0u) {
                scale = SpreadCurve(scale_curve + set, t, MaterialFloat(material, 128u),
                                    scale_random).xy;
            } else {
                const vec2 low = MaterialVector(material, 24u).xy;
                const vec2 high = MaterialVector(material, 28u).xy;
                scale = fma(high - low, vec2(scale_random), low);
            }
            // The colour: the material's curve 125, or a random mix of its words 16 to 19 and 20
            // to 23; with the rotation mode 3 times the particle's turn.
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
            const uint mode = DescriptionWord(description, 49u) & 0xffu;
            const vec4 turn = ParticleVector(particle, 8u);
            vec4 place = ParticleVector(particle, 0u);
            color = color * (mode == 3u ? turn : vec4(1.0));
            // The direction (the turn in mode 0, else the motion) and up: the turn in mode 1,
            // turned by the axis and angle in mode 2 or the angles in mode 5, else y.
            vec3 direction = mode == 0u ? turn.xyz : motion.xyz;
            vec3 up = turn.xyz;
            if (mode != 1u) {
                if (mode == 2u) {
                    const float turns = uintBitsToFloat(0x3e22f983u) * (0.5 * turn.w);
                    const float s = sin(6.283185307 * fract(turns));
                    const float axis_z = sqrt(turn.y * -turn.y + (-turn.x * turn.x + 1.0));
                    const vec4 q = vec4(turn.x * s, s * turn.y, s * axis_z,
                                        cos(6.283185307 * fract(abs(turns))));
                    up = AxisUp(q, s, axis_z);
                } else if (mode == 5u) {
                    up = EulerUp(EulerTurn(turn.xyz));
                } else {
                    up = vec3(0.0, 1.0, 0.0);
                }
            }
            // In the emitter's frame: the size times the emitter's scale, the direction turned
            // and scaled, the place turned, scaled and moved, and up turned.
            const vec4 emitter_place = EmitterVector(emitter, 0u);
            const bool emitter_frame = DescriptionWord(description, 18u) == 1u;
            if (emitter_frame) {
                const vec4 emitter_turn = EmitterVector(emitter, 4u);
                place.w = emitter_place.w * place.w;
                direction = emitter_place.w * Turned(emitter_turn, direction);
                place.xyz = fma(vec3(emitter_place.w), Turned(emitter_turn, place.xyz),
                                emitter_place.xyz);
                up = Turned(emitter_turn, up);
            }
            const uint map_flags = MaterialWord(material, 120u);
            const bool scales = (DescriptionWord(description, 3u) & 0x100u) != 0u;
            const float model_scale = EmitterFloat(emitter, 25u);
            const float frame_scale = emitter_frame ? emitter_place.w : 1.0;
            // The billboard mode (word 120's top byte): the corners' two centres a and b, the
            // angle, its cosine and sine in the turn of the corners' offsets, and their height.
            vec3 a = vec3(0.0);
            vec3 b = vec3(0.0);
            vec4 turns = vec4(1.0, 0.0, 0.0, 1.0);
            float height = scale.y;
            const uint billboard = bitfieldExtract(map_flags, 24, 8);
            if (billboard == 0u) {
                // Spun by angles in degrees: the material's curve 126, or a random start in
                // words 32 and 33 plus a random speed in 34 and 35 times the time elapsed; plus
                // the turn's angle in the rotation mode 2.
                a = ViewPosition(place.xyz, ViewTranslation());
                float degrees;
                const float spin_random = SeedRandom(seed + 0x1a57u);
                const uint spin_curve = MaterialWord(material, 126u);
                if (spin_curve != 0u) {
                    degrees = SpreadCurve(spin_curve + set, t, MaterialFloat(material, 130u),
                                          spin_random).x;
                } else {
                    const vec4 spin = MaterialVector(material, 32u);
                    const float speed = fma(SeedRandom(seed + 0x2656u), spin.w - spin.z, spin.z);
                    const float start = fma(spin_random, spin.y - spin.x, spin.x);
                    degrees = fma(speed, elapsed, start);
                }
                const float radians =
                    degrees * uintBitsToFloat(0x3c8ef921u) + (mode == 2u ? turn.w : 0.0);
                const float sine =
                    sin(6.283185307 * fract(uintBitsToFloat(0x3e22f983u) * radians));
                const float cosine = sin(6.283185307 * fract(fma(radians,
                                                                 uintBitsToFloat(0x3e22f983u),
                                                                 uintBitsToFloat(0x3e7ffe10u))));
                turns = vec4(cosine, sine, 0.0 - sine, cosine);
                angle = FlippedSign(radians);
                b = a;
                if (scales) {
                    const float model = (MaterialWord(material, 136u) & 0x4u) == 0u
                                            ? model_scale
                                            : 1.0;
                    place.w = (place.w * model) * (1.0 / frame_scale);
                }
            } else if (billboard == 1u) {
                // Along the direction: from the place towards its tip, stretched.
                const vec3 tip = direction + place.xyz;
                const float tip_x = MatrixRow(0u, tip, DrawFloat(3u));
                b.x = MatrixRow(0u, place.xyz, DrawFloat(3u));
                const float tip_y = MatrixRow(4u, tip, DrawFloat(7u));
                b.y = MatrixRow(4u, place.xyz, DrawFloat(7u));
                const vec3 screen = ScreenDirection(b.xy, vec2(tip_x, tip_y));
                angle = GameAtan2(screen.x, screen.y);
                b.z = MatrixRow(8u, place.xyz, DrawFloat(11u));
                turns = vec4(screen.y, FlippedSign(screen.x), 0.0, 1.0);
                if (scales) {
                    place.w = place.w * (1.0 / frame_scale);
                }
                const float length = sqrt(screen.z);
                const float half_length = ((height * length) * (1.0 / (height + length))) * 0.5;
                a = vec3(-screen.x * half_length + b.x, -screen.y * half_length + b.y, b.z);
                b.xy = vec2(half_length * screen.x + b.x, half_length * screen.y + b.y);
                height = 0.0;
            } else if (billboard == 2u) {
                // Towards a target: the emitter's place, or in the rotation mode 4 the turn
                // (plus the emitter's place in its frame) with the turn's w.
                b = ViewPosition(place.xyz, ViewTranslation());
                const vec4 target =
                    vec4((mode == 4u ? turn.xyz : emitter_place.xyz) +
                             (mode == 4u && emitter_frame ? emitter_place.xyz : vec3(0.0)),
                         mode == 4u ? turn.w : 1.0);
                a = vec3(TargetRow(0u, target), TargetRow(4u, target), TargetRow(8u, target));
                const vec3 screen = ScreenDirection(a.xy, b.xy);
                angle = GameAtan2(screen.x, screen.y);
                turns = vec4(screen.y, FlippedSign(screen.x), 0.0, 1.0);
                height = 0.0;
                if (scales) {
                    place.w = (place.w * model_scale) * (1.0 / frame_scale);
                }
            } else if (billboard == 3u) {
                // Along up: from the place to its tip.
                const vec3 tip = up + place.xyz;
                b.xy = vec2(MatrixRow(0u, tip, DrawFloat(3u)), MatrixRow(4u, tip, DrawFloat(7u)));
                a.xy = vec2(MatrixRow(0u, place.xyz, DrawFloat(3u)),
                            MatrixRow(4u, place.xyz, DrawFloat(7u)));
                const vec3 screen = ScreenDirection(a.xy, b.xy);
                angle = GameAtan2(screen.x, screen.y);
                b.z = MatrixRow(8u, tip, DrawFloat(11u));
                a.z = MatrixRow(8u, place.xyz, DrawFloat(11u));
                turns = vec4(screen.y, FlippedSign(screen.x), 0.0, 1.0);
                height = 0.0;
                // The factor picked as bits keeps the product's signed zero.
                place.w = place.w * uintBitsToFloat(scales ? 0u : 0x3f800000u);
            }
            // The corners' offsets: half the size times the scale, turned, and all of it pulled
            // towards the eye by c_materialAttr.z over the view depth.
            const float pull = -DrawFloat(74u) * (1.0 / b.z) + 1.0;
            const float half_size = 0.5 * place.w;
            const float half_width = scale.x * half_size;
            const float half_height = height * half_size;
            const vec2 across = vec2((turns.x * pull) * half_width, (turns.y * pull) * half_width);
            const vec2 along = vec2((turns.z * pull) * half_height, (turns.w * pull) * half_height);
            const vec3 b_pulled = vec3(b.x * pull, b.y * pull, b.z * pull);
            // The four maps' windows; each corner takes its texture coordinates from them in
            // its own branch.
            vec4 windows[4];
            bool turned[4];
            for (uint map = 0u; map < 4u; map++) {
                const uint flags = MaterialWord(material, 120u + map);
                windows[map] = MapWindow(material, seed, t, elapsed, map);
                turned[map] = bitfieldExtract(flags, 6, 2) != 0u &&
                              bitfieldExtract(flags, 3, 1) != 0u;
            }
            const float turn_length = sqrt(turn.z * turn.z + (turn.y * turn.y + turn.x * turn.x));
            vec2 coordinates[4];
            if (corner != 0u) {
                if (corner != 1u) {
                    const vec3 a_pulled = vec3(a.x * pull, a.y * pull, a.z * pull);
                    if (corner == 3u) {
                        view = vec3(a_pulled.x + across.x - along.x,
                                    a_pulled.y + across.y - along.y, a_pulled.z);
                        coordinates = SpriteCorners(windows, turned, true, true, angle);
                        map0.zw = vec2(1.0, 1.0);
                    } else {
                        view = vec3(a_pulled.x - along.x - across.x,
                                    Difference(a_pulled.y, along.y, across.y), a_pulled.z);
                        coordinates = SpriteCorners(windows, turned, false, true, angle);
                        map0.zw = vec2(-1.0, 1.0);
                    }
                } else {
                    view = vec3(b_pulled.x + across.x + along.x, b_pulled.y + across.y + along.y,
                                b_pulled.z);
                    coordinates = SpriteCorners(windows, turned, true, false, angle);
                    map0.zw = vec2(1.0, -1.0);
                }
            } else {
                view = vec3(b_pulled.x + along.x - across.x, b_pulled.y + along.y - across.y,
                            b_pulled.z);
                coordinates = SpriteCorners(windows, turned, false, false, angle);
                map0.zw = vec2(-1.0, -1.0);
            }
            map0.xy = coordinates[0];
            map1 = vec3(coordinates[1], turn_length);
            map2 = coordinates[2];
            map3 = coordinates[3];
            distance = FlippedSign(view.z);
            width = place.w * scale.x;
            shown = 1.0;
            records = uvec4(effect, emitter, description, particle);
            set_bits = uvec4(set, set, set + 0x8000u, 0u);
            clip = ClipPosition(view);
            color = ListedColor(emitter, color);
        }
    }
    attr10 = uintBitsToFloat(set_bits);
    attr9 = uintBitsToFloat(records);
    attr8 = vec4(view, shown);
    attr7 = vec4(shown, 0.0, 0.0, 0.0);
    attr6 = vec4(0.0, shown, 0.0, t);
    attr5 = vec4(map3, 0.0, 0.0);
    attr4 = vec4(map2, 0.0, 0.0);
    attr3 = vec4(map1, 0.0);
    attr2 = map0;
    attr1 = vec4(distance, angle, 0.0, width);
    attr0 = color;
    return clip;
}
