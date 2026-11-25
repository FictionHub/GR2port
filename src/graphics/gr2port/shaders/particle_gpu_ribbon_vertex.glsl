// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The GPU particle ribbon vertex program (0x1b69300, in both passes): each particle draws the
// strip from itself to the next particle of its emitter's ring (10 vertices, five steps along it,
// two sides), bent along the circles through the particles before and after, with the width and
// colour of a Catmull-Rom spline through the four particles' sizes and colours. Include
// particle_gpu_sprite_vertex.glsl first.

// The game's unsigned division as its program computes it: a divisor of 0 gives 0xffffffff.
uint RibbonDivide(uint n, uint d) {
    return d != 0u ? n / d : 0xffffffffu;
}

// The remainder that goes with RibbonDivide: n itself for a divisor of 0.
uint RibbonModulo(uint n, uint d) {
    return n - RibbonDivide(n, d) * d;
}

struct RibbonChain {
    // Whether the particle shows, and its record, emitter, description and raw seed (word 13).
    bool shown;
    uint particle;
    uint emitter;
    uint description;
    uint seed;
    // Its life, the time left (word 7), the time elapsed, the life's inverse and its age t.
    float life;
    float left;
    float elapsed;
    float to_life;
    float t;
    // The emitter's effect, the material set and the material.
    uint effect;
    uint set;
    uint material;
    // The colour at t: the material's curve 125, or a random mix of its words 16 to 23.
    vec4 color;
    // The emitter's place and scale, and the ring's first particle (emitter word 12).
    vec4 emitter_place;
    uint first;
    // The next particle, the one after it, the previous one and the ring's head.
    uint next;
    uint next_next;
    uint previous;
    uint head;
    // The seed of the ribbon's flipbook (emitter word 28, or the base particle + 1 under a
    // parent), its generation (word 28 mod 15 + 1, or bits 28-30 of the parent's particle) and
    // its position along the emitter's range (or the base particle's turn w).
    uint ribbon_seed;
    uint generation;
    float position;
    // Where the ribbon starts: the emitter's place, or the base particle's under a parent.
    vec3 anchor;
    // The time elapsed held to 0 to the life, and over the life.
    float held_elapsed;
    float held_t;
};

// A point turned by a quaternion in the ribbon program's order: its first stage rounds the other
// product of each pair than particle_gpu_vertex.glsl's Turned does.
vec3 RibbonTurned(vec4 q, vec3 p) {
    const float t1 = q.w * p.y + (p.z * -q.x + p.x * q.z);
    const float t2 = q.w * p.z + (q.y * -p.x + q.x * p.y);
    const float t0 = q.w * p.x + (-p.y * q.z + p.z * q.y);
    const float d = q.z * p.z + (p.y * q.y + q.x * p.x);
    return vec3(d * q.x + (q.w * t0 + (t2 * q.y + -q.z * t1)),
                q.y * d + (q.w * t1 + (t0 * q.z + -q.x * t2)),
                q.z * d + (q.w * t2 + (t1 * q.x + -q.y * t0)));
}

// The particle of this instance and its ring, or not shown.
RibbonChain RibbonStart() {
    RibbonChain chain;
    chain.shown = false;
    const uint instance = uint(gl_InstanceIndex) & 0xfffffu;
    uint particle = DrawWords().words[80] + instance;
    if (DrawFloat(75u) >= uintBitsToFloat(0x3f7ffffeu)) {
        // The list holds a word a particle of the pool; past it, particle 0.
        particle = TableWord(instance_address, particle, ParticleCount, 4u, 0u);
    }
    chain.particle = particle;
    const float left = ParticleFloat(particle, 7u);
    if (left <= 0.0) {
        return chain;
    }
    const uint emitter = ParticleWord(particle, 12u) & 0xffffffu;
    if (emitter == 0u) {
        return chain;
    }
    const uint kind = EmitterWord(emitter, 10u);
    if (kind != 0u && kind != 1u) {
        return chain;
    }
    const uint description = EmitterWord(emitter, 8u);
    const vec4 links = EmitterVector(emitter, 24u);
    // A parent emitter (word 27, a float) where description word 16 is not 0xff.
    const bool parented = DescriptionWord(description, 16u) != 0xffu;
    uint parent = 0u;
    if (parented) {
        parent = GameUintOf(links.w);
        if (parent == 0u) {
            return chain;
        }
        const uint parent_kind = EmitterWord(parent, 10u);
        if (parent_kind != 0u && parent_kind != 1u) {
            return chain;
        }
    }
    // The particle's life from the description's shortest to longest by its hash, its age t.
    const uint seed = ParticleWord(particle, 13u);
    const float shortest = DescriptionFloat(description, 39u);
    const float longest = DescriptionFloat(description, 43u);
    const float life = fma(SeedHash(seed) * (longest - shortest), uintBitsToFloat(0x3ec01c14u),
                           shortest);
    const float elapsed = life - left;
    const float to_life = 1.0 / life;
    const float t = to_life * elapsed;
    if (t < 0.0 || t > 1.0) {
        return chain;
    }
    const uint effect = EmitterWord(emitter, 9u);
    const uint set = EffectWord(effect, 45u) << 6;
    const uint material = DrawWords().words[87] + set;
    const float color_random = SeedRandom(DrawWords().words[87] + seed + 0x4d2u);
    const uint color_curve = MaterialWord(material, 125u);
    if (color_curve != 0u) {
        chain.color =
            SpreadCurve(color_curve + set, t, MaterialFloat(material, 129u), color_random);
    } else {
        const vec4 low = MaterialVector(material, 16u);
        const vec4 high = MaterialVector(material, 20u);
        chain.color = fma(high - low, vec4(color_random), low);
    }
    // The ring of particles: under a parent emitter each parent particle owns `per` of them,
    // the first of each run the base; else the emitter's ring of `ring` from its first.
    const uint ring = DescriptionWord(description, 6u);
    const vec4 emitter_place = EmitterVector(emitter, 0u);
    const uint first = EmitterWord(emitter, 12u);
    const uint index = particle - first;
    vec3 anchor = emitter_place.xyz;
    if (parented) {
        const uint parent_description = EmitterWord(parent, 8u);
        const uint per = RibbonDivide(ring, DescriptionWord(parent_description, 6u));
        const uint run = RibbonDivide(index, per);
        const uint base = first + per * run;
        const uint after = base + 1u;
        const uint step = particle - after;
        chain.previous = after + RibbonModulo(per + step - 2u, per - 1u);
        chain.next = after + RibbonModulo(step + 1u, per - 1u);
        chain.next_next = after + RibbonModulo(step + 2u, per - 1u);
        chain.head = after + ParticleWord(base, 13u);
        anchor = ParticleVector(base, 0u).xyz;
        if (DescriptionWord(description, 18u) == 1u &&
            DescriptionWord(parent_description, 18u) == 1u) {
            const vec4 parent_place = EmitterVector(parent, 0u);
            anchor = fma(vec3(parent_place.w), RibbonTurned(EmitterVector(parent, 4u), anchor),
                         parent_place.xyz);
        }
        chain.generation =
            bitfieldExtract(ParticleWord(EmitterWord(parent, 12u) + run, 12u), 28, 3);
        chain.position = ParticleFloat(base, 11u);
        chain.ribbon_seed = after;
    } else {
        chain.previous = first + RibbonModulo(ring + index - 1u, ring);
        chain.next = first + RibbonModulo(index + 1u, ring);
        chain.next_next = first + RibbonModulo(index + 2u, ring);
        chain.head = first + EmitterWord(emitter, 14u);
        chain.ribbon_seed = EmitterWord(emitter, 28u);
        chain.generation = chain.ribbon_seed % 15u + 1u;
        const float low = DescriptionFloat(description, 0u);
        chain.position = (EmitterFloat(emitter, 11u) - low) *
                         (1.0 / (uintBitsToFloat(0x3727c5acu) +
                                 (DescriptionFloat(description, 1u) - low)));
    }
    chain.shown = true;
    chain.emitter = emitter;
    chain.description = description;
    chain.seed = seed;
    chain.life = life;
    chain.left = left;
    chain.elapsed = elapsed;
    chain.to_life = to_life;
    chain.t = t;
    chain.effect = effect;
    chain.set = set;
    chain.material = material;
    chain.emitter_place = emitter_place;
    chain.first = first;
    chain.anchor = anchor;
    // The time elapsed held to 0 to the life, and over the life.
    chain.held_elapsed = max(0.0, min(life, elapsed));
    chain.held_t = clamp(to_life * chain.held_elapsed, 0.0, 1.0);
    return chain;
}

struct RibbonPoints {
    // The places (w the size) and turns of this particle, the next, the one after and the
    // previous; a missing particle takes the one before it in the chain (this one for the
    // previous).
    vec4 this_place;
    vec4 next_place;
    vec4 nn_place;
    vec4 prev_place;
    vec4 this_turn;
    vec4 next_turn;
    vec4 nn_turn;
    uint next_seed;
    uint nn_seed;
    bool next_missing;
    bool nn_missing;
    bool prev_missing;
    // The next two particles' time left, age t and time elapsed held to their lives.
    float next_left;
    float nn_left;
    float next_t;
    float nn_t;
    float next_held;
    float nn_held;
    // The ups of this particle, the next and the one after, by the rotation mode.
    vec3 this_up;
    vec3 next_up;
    vec3 nn_up;
};

// A particle's up by the rotation mode: 1 the turn, 2 an axis and angle, 5 angles in degrees, else
// y.
vec3 RibbonUp(uint mode, vec4 turn) {
    if (mode == 1u) {
        return turn.xyz;
    }
    if (mode == 2u) {
        const float turns = uintBitsToFloat(0x3e22f983u) * (0.5 * turn.w);
        const float s = sin(6.283185307 * fract(turns));
        const float axis_z = sqrt(turn.y * -turn.y + (-turn.x * turn.x + 1.0));
        const vec4 q = vec4(turn.x * s, s * turn.y, s * axis_z,
                            cos(6.283185307 * fract(abs(turns))));
        return AxisUp(q, s, axis_z);
    }
    if (mode == 5u) {
        return EulerUp(EulerTurn(turn.xyz));
    }
    return vec3(0.0, 1.0, 0.0);
}

// The four particles in the emitter's frame where the description asks.
RibbonPoints RibbonGather(RibbonChain chain) {
    RibbonPoints p;
    const uint next_emitter = ParticleWord(chain.next, 12u) & 0xffffffu;
    const uint nn_emitter = ParticleWord(chain.next_next, 12u) & 0xffffffu;
    p.next_missing = next_emitter == 0u;
    p.nn_missing = nn_emitter == 0u;
    p.prev_missing = (ParticleWord(chain.previous, 12u) & 0xffffffu) == 0u;
    p.next_seed = p.next_missing ? chain.seed : ParticleWord(chain.next, 13u);
    p.nn_seed = p.nn_missing ? p.next_seed : ParticleWord(chain.next_next, 13u);
    const float shortest = DescriptionFloat(chain.description, 39u);
    const float span = DescriptionFloat(chain.description, 43u) - shortest;
    const float nn_life = fma(span * SeedHash(p.nn_seed), uintBitsToFloat(0x3ec01c14u), shortest);
    p.next_left = p.next_missing ? chain.left : ParticleFloat(chain.next, 7u);
    const float next_life =
        fma(span * SeedHash(p.next_seed), uintBitsToFloat(0x3ec01c14u), shortest);
    p.nn_left = p.nn_missing ? p.next_left : ParticleFloat(chain.next_next, 7u);
    p.nn_held = max(0.0, min(nn_life, nn_life - p.nn_left));
    p.next_held = max(0.0, min(next_life, next_life - p.next_left));
    p.nn_t = clamp((1.0 / nn_life) * p.nn_held, 0.0, 1.0);
    p.next_t = clamp((1.0 / next_life) * p.next_held, 0.0, 1.0);
    p.this_place = ParticleVector(chain.particle, 0u);
    p.this_turn = ParticleVector(chain.particle, 8u);
    p.next_place = p.next_missing ? p.this_place : ParticleVector(chain.next, 0u);
    p.next_turn = p.next_missing ? p.this_turn : ParticleVector(chain.next, 8u);
    p.nn_place = p.nn_missing ? p.next_place : ParticleVector(chain.next_next, 0u);
    p.nn_turn = p.nn_missing ? p.next_turn : ParticleVector(chain.next_next, 8u);
    p.prev_place = p.prev_missing ? p.this_place : ParticleVector(chain.previous, 0u);
    if (DescriptionWord(chain.description, 18u) == 1u) {
        const float scale = chain.emitter_place.w;
        const vec4 q = EmitterVector(chain.emitter, 4u);
        p.nn_place = vec4(fma(vec3(scale), RibbonTurned(q, p.nn_place.xyz),
                              chain.emitter_place.xyz),
                          scale * p.nn_place.w);
        p.next_place = vec4(fma(vec3(scale), RibbonTurned(q, p.next_place.xyz),
                                chain.emitter_place.xyz), scale * p.next_place.w);
        p.this_place = vec4(fma(vec3(scale), RibbonTurned(q, p.this_place.xyz),
                                chain.emitter_place.xyz), scale * p.this_place.w);
        p.prev_place = vec4(fma(vec3(scale), RibbonTurned(q, p.prev_place.xyz),
                                chain.emitter_place.xyz), scale * p.prev_place.w);
    }
    const uint mode = DescriptionWord(chain.description, 49u) & 0xffu;
    p.this_up = RibbonUp(mode, p.this_turn);
    p.next_up = RibbonUp(mode, p.next_turn);
    p.nn_up = RibbonUp(mode, p.nn_turn);
    return p;
}

// The segment runs from C (this) to B (next); A (after next) and D (before this) bend it.
struct RibbonEnds {
    // A after the next particle, B the next and D before this one (w the size), with the
    // ribbon's end and start fixed up.
    vec4 a;
    vec4 b;
    vec4 d;
    // The ribbon position where the generation matches, else 1.
    float position;
    // 0 where the ribbon has no start: everything collapses onto this particle.
    uint start;
    // The next particle alive and not the head.
    bool next_ok;
    // 1, or 0 at the ribbon's end with the material's flag 2 (word 136).
    float end_alpha;
    // The next particle's age, and the next two particles' time held (1 and 0 at the end).
    float next_t;
    float next_held;
    float nn_held;
    // The ups of the next two particles (this one's at the end).
    vec3 next_up;
    vec3 nn_up;
    // A less B and B less this particle.
    vec3 to_a;
    vec3 to_b;
};

// The particles around the segment at the ribbon's ends: past the head the anchor (or the
// segment carried on), before the first a unit step back.
RibbonEnds RibbonEndsOf(RibbonChain chain, RibbonPoints p) {
    RibbonEnds e;
    const uint this_word = ParticleWord(chain.particle, 12u);
    const bool matches = chain.generation == bitfieldExtract(this_word, 24, 4);
    const uint flags = MaterialWord(chain.material, 136u);
    // The generation's ribbon keeps its end unless the material's flag 1 lets it go.
    const bool keep = matches && (flags & 1u) == 0u;
    e.position = matches ? chain.position : 1.0;
    e.start = 1u;
    e.a = p.nn_place;
    e.b = p.next_place;
    e.to_b = p.next_place.xyz - p.this_place.xyz;
    const bool next_end = 0.0 >= p.next_left || chain.next == chain.head;
    e.next_ok = !next_end;
    const bool next_alive = 0.0 < p.next_left && !next_end;
    const bool nn_end = 0.0 >= p.nn_left || chain.next_next == chain.head;
    if (next_alive && nn_end) {
        e.a = vec4(chain.anchor, p.next_place.w);
        if (!keep) {
            e.start = 1.0 > p.next_t ? 1u : 0u;
            e.a = vec4(p.next_place.xyz + e.to_b, p.nn_place.w);
        }
    }
    // From B to the particle after it (not to A), unless the ribbon ends.
    e.to_a = p.nn_place.xyz - p.next_place.xyz;
    e.end_alpha = 1.0;
    e.next_t = p.next_t;
    e.next_held = p.next_held;
    e.nn_held = p.nn_held;
    e.next_up = p.next_up;
    e.nn_up = p.nn_up;
    if (0.0 >= p.next_left || chain.next == chain.head) {
        e.end_alpha = (flags & 2u) != 0u ? 0.0 : 1.0;
        uint start = 0u;
        if (keep) {
            const uint word = p.prev_missing ? this_word : ParticleWord(chain.previous, 12u);
            start = bitfieldExtract(word, 24, 4) == 0xfu ? 0u : e.start;
            const vec3 twice = vec3(chain.anchor.x + chain.anchor.x,
                                    chain.anchor.y + chain.anchor.y,
                                    chain.anchor.z + chain.anchor.z);
            e.a = vec4(twice - p.this_place.xyz, p.this_place.w);
            e.b = vec4(chain.anchor, p.this_place.w);
        }
        e.start = start;
        e.to_a = e.a.xyz - e.b.xyz;
        e.to_b = e.b.xyz - p.this_place.xyz;
        e.next_t = 1.0;
        e.nn_held = 0.0;
        e.next_held = 0.0;
        e.nn_up = p.this_up;
        e.next_up = p.this_up;
    }
    // Before this particle: the previous one, or a unit step back along the segment where the
    // previous one is gone or older and the next one is fine.
    e.d = p.prev_place;
    const float prev_left = p.prev_missing ? chain.left : ParticleFloat(chain.previous, 7u);
    if (e.next_ok && (0.0 >= prev_left || prev_left > chain.left)) {
        const vec3 back = p.this_place.xyz - e.b.xyz;
        const float inverse = 1.0 / sqrt(max(uintBitsToFloat(0x34000000u),
                                             fma(back.z, back.z,
                                                 fma(back.y, back.y, back.x * back.x))));
        e.d = vec4(fma(back, vec3(inverse), p.this_place.xyz), 0.0);
    }
    return e;
}


// A map's window (u and v at its two corners) by the map's mode (bits 6 and 7 of word 120 +
// map): 1 and 3 moved by the ribbon position, 2 a flipbook frame on the position with v along
// the ribbon, 0 a flipbook frame on time.
vec4 RibbonWindow(RibbonChain chain, uint map, float position, float this_t, float next_t,
                  uint next_seed) {
    const uint material = chain.material;
    const uint flags = MaterialWord(material, 120u + map);
    const uint mode = bitfieldExtract(flags, 6, 2);
    const uint words = 4u * map;
    if (mode != 0u && mode != 2u) {
        return MaterialVector(material, 68u + words) * position +
               MaterialVector(material, 52u + words);
    }
    const uint across = bitfieldExtract(flags, 8, 8);
    const uint down = bitfieldExtract(flags, 16, 8);
    // The flipbook's first frame: by the ribbon's seed along the ribbon; on time by this
    // particle's seed for maps 0 and 1 and the next particle's for maps 2 and 3.
    const uint seed =
        (mode == 2u ? chain.ribbon_seed : (map < 2u ? chain.seed : next_seed)) + 0x1a8eu;
    float first = 0.0;
    if (bitfieldExtract(flags, 1, 1) != 0u) {
        first = uintBitsToFloat(0x3ec01c14u) * (SeedHash(seed) * float(across * down));
    }
    const float start = MaterialFloat(material, 44u + map) + first;
    const float speed = MaterialFloat(material, 48u + map);
    float time = position;
    if (mode == 0u) {
        time = clamp(bitfieldExtract(flags, 2, 1) > 0u ? chain.held_elapsed : this_t, 0.0,
                     uintBitsToFloat(0x3f7fffefu));
    }
    const uint frame = GameUintOf(fma(time, speed, start));
    const uint line = RibbonDivide(frame, across);
    const float column = float(frame - line * across);
    const float row = float(RibbonModulo(line, down));
    const float to_across = 1.0 / float(across);
    const float to_down = 1.0 / float(down);
    if (mode == 2u) {
        // The rows run along the ribbon: this particle's age at the top, the next one's at the
        // bottom.
        return vec4(column * to_across, to_down * (row + this_t),
                    to_across * column + to_across, to_down * (row + next_t));
    }
    return vec4(column * to_across, row * to_down, to_across * column + to_across,
                to_down * row + to_down);
}

// A unit vector, its squared length held to 2^-23 at least.
vec3 RibbonUnit(vec3 v) {
    return v * (1.0 / sqrt(max(uintBitsToFloat(0x34000000u), v.z * v.z + (v.y * v.y + v.x * v.x))));
}

// Up turned about the direction by the angle in degrees: cos times up plus sin times the unit
// cross product scaled to up's length.
vec3 RibbonSpun(vec3 up, vec3 dir, float degrees) {
    const float cx = dir.z * -up.y + up.z * dir.y;
    const float cz = dir.y * -up.x + up.y * dir.x;
    const float cy = dir.x * -up.z + up.x * dir.z;
    const float inverse =
        1.0 / sqrt(max(uintBitsToFloat(0x34000000u), cz * cz + (cy * cy + cx * cx)));
    const float size = sqrt(up.z * up.z + (up.y * up.y + up.x * up.x));
    const float turns = uintBitsToFloat(0x3e22f983u) * (uintBitsToFloat(0x3c8ef921u) * degrees);
    const float s = sin(6.283185307 * fract(turns));
    const float co = cos(6.283185307 * fract(abs(turns)));
    return vec3(size * ((s * cx) * inverse) + co * up.x, size * ((s * cy) * inverse) + co * up.y,
                size * ((s * cz) * inverse) + co * up.z);
}

// A particle's spin in degrees: curve 126 at t, or a random start plus a random speed times the
// time held; raw seeds.
float RibbonDegrees(RibbonChain chain, uint seed, float t, float held) {
    const float random = SeedRandom(seed + 0x1a57u);
    const uint curve = MaterialWord(chain.material, 126u);
    if (curve != 0u) {
        return SpreadCurve(curve + chain.set, t, MaterialFloat(chain.material, 130u), random).x;
    }
    const vec4 spin = MaterialVector(chain.material, 32u);
    const float speed = (spin.w - spin.z) * SeedRandom(seed + 0x2656u) + spin.z;
    const float start = (spin.y - spin.x) * random + spin.x;
    return speed * held + start;
}

// Billboards 10 and 11: perpendiculars (z 0) of the segments' directions in x and y.
void RibbonFlatSides(vec3 a, vec3 b, vec3 c, vec3 d, out vec3 da, out vec3 db, out vec3 dc,
                     out vec3 dd) {
    const vec3 ab = a - b;
    const vec3 bc = b - c;
    const vec3 cd = c - d;
    const float small = uintBitsToFloat(0x34000000u);
    const float inv_ab = 1.0 / sqrt(max(small, ab.z * ab.z + (ab.y * ab.y + ab.x * ab.x)));
    const float inv_cd = 1.0 / sqrt(max(small, cd.z * cd.z + (cd.y * cd.y + cd.x * cd.x)));
    const float inv_bc = 1.0 / sqrt(max(small, bc.z * bc.z + (bc.y * bc.y + bc.x * bc.x)));
    const vec2 pa = vec2(inv_ab * ab.y, -inv_ab * ab.x);
    const vec2 pd = vec2(inv_cd * cd.y, -inv_cd * cd.x);
    const vec2 pb = vec2(inv_bc * bc.y + pa.x, -inv_bc * bc.x + pa.y);
    const vec2 pc = vec2(inv_bc * bc.y + pd.x, cd.x * -inv_cd + -inv_bc * bc.x);
    da = vec3(pa * (1.0 / sqrt(max(small, pa.y * pa.y + pa.x * pa.x))), 0.0);
    db = vec3(pb * (1.0 / sqrt(max(small, pb.y * pb.y + pb.x * pb.x))), 0.0);
    dc = vec3(pc * (1.0 / sqrt(max(small, pc.y * pc.y + pc.x * pc.x))), 0.0);
    dd = vec3(pd * (1.0 / sqrt(max(small, pd.y * pd.y + pd.x * pd.x))), 0.0);
}

// The circle through d, c and b: its centre in xyz and 1 in w, or 0 where the triangle or a side
// is too small (squared lengths or the weights' sum below 0.01). Each corner's weight is its far
// side squared times the other two squared less it.
vec4 RibbonCircle(vec3 d, vec3 c, vec3 b) {
    const vec3 cd = c - d;
    const vec3 bc = b - c;
    const float x = bc.y * -cd.z + cd.y * bc.z;
    const float y = bc.z * -cd.x + cd.z * bc.x;
    const float z = bc.x * -cd.y + cd.x * bc.y;
    const float limit = uintBitsToFloat(0x3c23d70au);
    if (z * z + (y * y + x * x) < limit) {
        return vec4(0.0);
    }
    const float a2 = bc.z * bc.z + (bc.y * bc.y + bc.x * bc.x);
    if (a2 < limit) {
        return vec4(0.0);
    }
    const vec3 db = d - b;
    const float b2 = db.z * db.z + (db.y * db.y + db.x * db.x);
    if (b2 < limit) {
        return vec4(0.0);
    }
    const float c2 = cd.z * cd.z + (cd.y * cd.y + cd.x * cd.x);
    if (c2 < limit) {
        return vec4(0.0);
    }
    const float wd = a2 * ((b2 + c2) - a2);
    const float rest_c = (a2 + c2) - b2;
    const float rest_b = (a2 + b2) - c2;
    const float sum = rest_b * c2 + (b2 * rest_c + wd);
    if (abs(sum) < limit) {
        return vec4(0.0);
    }
    const float wc = b2 * rest_c;
    const float wb = c2 * rest_b;
    const float inverse = 1.0 / sum;
    return vec4(inverse * (wb * b.x + (wc * c.x + d.x * wd)),
                inverse * (b.y * wb + (wc * c.y + d.y * wd)),
                inverse * (b.z * wb + (wc * c.z + d.z * wd)), 1.0);
}

// The point u along c to b, moved onto the circle through c where there is one.
vec3 RibbonArcPoint(vec4 circle, vec3 c, vec3 b, float u) {
    const vec3 along = (b - c) * u + c;
    if (circle.w == 0.0) {
        return along;
    }
    const vec3 from = along - circle.xyz;
    const vec3 radius = c - circle.xyz;
    const float k = (1.0 / sqrt(from.z * from.z + (from.y * from.y + from.x * from.x))) *
                    sqrt(radius.z * radius.z + (radius.y * radius.y + radius.x * radius.x));
    return k * from + circle.xyz;
}

// A side's point at u: on the circle through d, c and b and on the one through c, b and a,
// blended by u.
vec3 RibbonSidePoint(vec3 d, vec3 c, vec3 b, vec3 a, float u) {
    const vec3 first = RibbonArcPoint(RibbonCircle(d, c, b), c, b, u);
    const vec3 second = RibbonArcPoint(RibbonCircle(c, b, a), c, b, u);
    return u * (second - first) + first;
}

// The width at u: a Catmull-Rom spline through the four sizes, halved.
float RibbonSize(float d, float c, float b, float a, float u, float u2, float u3) {
    const float quad = (4.0 * b + (5.0 * -c + 2.0 * d)) - a;
    const float cubic = 3.0 * -b + (c * 3.0 + (a - d));
    return 0.25 * (cubic * u3 + (quad * u2 + ((b - d) * u + 2.0 * c)));
}

// A colour channel at u: the Catmull-Rom spline through the four, d and a times their turns'
// factors.
float RibbonBlend(float d, float d_turn, float c, float b, float a, float a_turn, float u,
                  float u2, float u3) {
    const float turned_d = d * d_turn;
    const float quad = a_turn * -a + (4.0 * b + (5.0 * -c + 2.0 * turned_d));
    const float cubic = 3.0 * -b + (c * 3.0 + (a * a_turn + -turned_d));
    return (cubic * u3 + (u2 * quad + ((-d * d_turn + b) * u + 2.0 * c))) * 0.5;
}

// a * b rounded, then plus c: the game's program leaves this pair unfused where the driver
// would otherwise fuse it. (A precise inside a function stays there.)
float RibbonUnfused(float a, float b, float c) {
    precise const float product = a * b;
    return product + c;
}

// GameAtan2 with its last step fused, as the game's program computes it for the strip's minus
// side between the ends.
float RibbonAtan2(float x, float y) {
    const float ratio = (1.0 / max(abs(x), abs(y))) * min(abs(x), abs(y));
    const float square = ratio * ratio;
    float p = uintBitsToFloat(0x3caaae29u) * square + uintBitsToFloat(0xbdae5a36u);
    p = p * square + uintBitsToFloat(0x3e3876e2u);
    p = p * square + uintBitsToFloat(0xbea91cf3u);
    p = p * square + uintBitsToFloat(0x3f7ff738u);
    const float steep = abs(y) < abs(x) ? -2.0 * (ratio * p) + uintBitsToFloat(0x3fc90fd8u) : 0.0;
    const float below = y < -y ? uintBitsToFloat(0xc0490fdcu) : 0.0;
    const float angle = fma(p, ratio, below + steep);
    return min(x, y) < -min(x, y) && max(x, y) >= -max(x, y) ? FlippedSign(angle) : angle;
}

// Writes the outputs and gives the clip position.
vec4 ParticleRibbonVertex() {
    draw_index = uint(gl_BaseInstance) >> 20;
    vec4 color = vec4(0.0);
    vec4 maps[4] = vec4[4](vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0));
    vec4 marks = vec4(0.0);
    float angle = 0.0;
    float pull = 0.0;
    vec3 pulled = vec3(0.0);
    float depth = 0.0;
    float shown = 0.0;
    float t = 0.0;
    uvec4 records = uvec4(0u);
    uvec4 set_bits = uvec4(0u);
    vec4 clip = vec4(0.0);
    const uint vertex = uint(gl_VertexIndex);
    const RibbonChain chain = RibbonStart();
    if (chain.shown) {
        const RibbonPoints p = RibbonGather(chain);
        const RibbonEnds e = RibbonEndsOf(chain, p);
        const uint material = chain.material;
        const uint mode = DescriptionWord(chain.description, 49u) & 0xffu;
        // Colours of D, C, B and A from the raw seeds.
        const uint prev_seed =
            p.prev_missing ? chain.seed : ParticleWord(chain.previous, 13u);
        const float prev_random = SeedRandom(prev_seed + 0x4d2u);
        const float this_random = SeedRandom(chain.seed + 0x4d2u);
        const float next_random = SeedRandom(p.next_seed + 0x4d2u);
        const float nn_random = SeedRandom(p.nn_seed + 0x4d2u);
        vec4 color_d;
        vec4 color_c;
        vec4 color_b;
        vec4 color_a;
        const uint color_curve = MaterialWord(material, 125u);
        if (color_curve != 0u) {
            const float shortest = DescriptionFloat(chain.description, 39u);
            const float span = DescriptionFloat(chain.description, 43u) - shortest;
            const float prev_life =
                fma(span * SeedHash(prev_seed), uintBitsToFloat(0x3ec01c14u), shortest);
            const float prev_left =
                p.prev_missing ? chain.left : ParticleFloat(chain.previous, 7u);
            const float prev_t = clamp(
                (1.0 / prev_life) * max(0.0, min(prev_life, prev_life - prev_left)), 0.0, 1.0);
            const uint curve = color_curve + chain.set;
            const float spread = MaterialFloat(material, 129u);
            color_d = SpreadCurve(curve, prev_t, spread, prev_random);
            color_a = SpreadCurve(curve, p.nn_t, spread, nn_random);
            color_b = SpreadCurve(curve, e.next_t, spread, next_random);
            color_c = SpreadCurve(curve, chain.held_t, spread, this_random);
        } else {
            const vec4 low = MaterialVector(material, 16u);
            const vec4 high = MaterialVector(material, 20u);
            color_a = fma(high - low, vec4(nn_random), low);
            color_b = fma(high - low, vec4(next_random), low);
            color_c = fma(high - low, vec4(this_random), low);
            color_d = fma(high - low, vec4(prev_random), low);
        }
        const vec4 this_factor = mode == 3u ? p.this_turn : vec4(1.0);
        const vec4 next_factor = mode == 3u ? p.next_turn : vec4(1.0);
        color_c = color_c * this_factor;
        color_b = vec4(color_b.rgb * next_factor.rgb, (e.end_alpha * color_b.a) * next_factor.a);
        color_a.a = e.end_alpha * color_a.a;
        // The scales of this particle and the next.
        float this_scale;
        float next_scale;
        const float this_scale_random = SeedRandom(chain.seed + 0xd80u);
        const float next_scale_random = SeedRandom(p.next_seed + 0xd80u);
        const uint scale_curve = MaterialWord(material, 124u);
        if (scale_curve != 0u) {
            const float spread = MaterialFloat(material, 128u);
            next_scale =
                SpreadCurve(scale_curve + chain.set, e.next_t, spread, next_scale_random).x;
            this_scale =
                SpreadCurve(scale_curve + chain.set, chain.held_t, spread, this_scale_random).x;
        } else {
            const float low = MaterialFloat(material, 24u);
            const float range = MaterialFloat(material, 28u) - low;
            next_scale = fma(range, next_scale_random, low);
            this_scale = fma(range, this_scale_random, low);
        }
        // A ribbon with no start collapses onto this particle.
        const bool start = e.start != 0u;
        const vec3 point_c = p.this_place.xyz;
        const vec3 point_a = start ? e.a.xyz : point_c;
        const vec3 point_b = start ? e.b.xyz : point_c;
        const vec3 point_d = start ? e.d.xyz : point_c;
        float size_d = this_scale * (start ? e.d.w : 0.0);
        float size_c = this_scale * (start ? p.this_place.w : 0.0);
        float size_b = next_scale * (start ? e.b.w : 0.0);
        float size_a = next_scale * (start ? e.a.w : 0.0);
        const float this_t = start ? chain.held_t : 0.0;
        const float next_t = start ? e.next_t : 0.0;
        const float nn_t = start ? p.nn_t : 0.0;
        // The maps' windows.
        vec4 windows[4];
        for (uint map = 0u; map < 4u; map++) {
            windows[map] = RibbonWindow(chain, map, e.position, this_t, next_t, p.next_seed);
        }
        // The side directions by the billboard mode (word 120's top byte).
        const uint billboard = bitfieldExtract(MaterialWord(material, 120u), 24, 8);
        vec3 dir_a = vec3(0.0, 1.0, 0.0);
        vec3 dir_b = vec3(0.0, 1.0, 0.0);
        vec3 dir_c = vec3(0.0, 1.0, 0.0);
        vec3 dir_d = vec3(0.0, 1.0, 0.0);
        vec3 a = point_a;
        vec3 b = point_b;
        vec3 c = point_c;
        vec3 d = point_d;
        bool in_view = false;
        bool one_sided = false;
        if (billboard == 10u) {
            RibbonFlatSides(a, b, c, d, dir_a, dir_b, dir_c, dir_d);
        } else if (billboard == 11u) {
            a = ViewPosition(a, ViewTranslation());
            b = ViewPosition(b, ViewTranslation());
            c = ViewPosition(c, ViewTranslation());
            d = ViewPosition(d, ViewTranslation());
            RibbonFlatSides(a, b, c, d, dir_a, dir_b, dir_c, dir_d);
            in_view = true;
        } else if (billboard == 12u || billboard == 13u) {
            const vec3 x_axis = vec3(1.0, 0.0, 0.0);
            const vec3 spun_a = RibbonSpun(
                e.nn_up, start ? e.to_a : x_axis,
                RibbonDegrees(chain, p.nn_seed, nn_t, e.nn_held));
            const vec3 spun_b = RibbonSpun(
                e.next_up, start ? e.to_b : x_axis,
                RibbonDegrees(chain, p.next_seed, next_t, e.next_held));
            const vec3 spun_c = RibbonSpun(
                p.this_up, start ? p.this_place.xyz - p.prev_place.xyz : x_axis,
                RibbonDegrees(chain, chain.seed, this_t, chain.held_elapsed));
            dir_a = RibbonUnit(spun_a);
            dir_b = RibbonUnit(spun_b);
            dir_c = RibbonUnit(spun_c);
            dir_d = dir_c;
            one_sided = billboard == 13u;
        }
        // The sizes in the model's scale where the description asks.
        if ((DescriptionWord(chain.description, 3u) & 0x100u) != 0u) {
            const float emitter_scale = chain.emitter_place.w;
            const float factor = (MaterialWord(material, 136u) & 4u) == 0u
                                     ? max(uintBitsToFloat(0x3c23d70au), emitter_scale)
                                     : 1.0;
            float model_scale = EmitterFloat(chain.emitter, 25u);
            if (DescriptionWord(chain.description, 18u) == 1u) {
                model_scale = model_scale * (1.0 / emitter_scale);
            }
            const float k = factor * model_scale;
            size_a = size_a * k;
            size_b = size_b * k;
            size_c = size_c * k;
            size_d = size_d * k;
        }
        // Each point's two sides.
        vec3 plus_a, plus_b, plus_c, plus_d, minus_a, minus_b, minus_c, minus_d;
        if (one_sided) {
            plus_a = fma(dir_a, vec3(size_a), a);
            plus_b = fma(dir_b, vec3(size_b), b);
            plus_c = fma(dir_c, vec3(size_c), c);
            plus_d = fma(dir_d, vec3(size_d), d);
            minus_a = a;
            minus_b = b;
            minus_c = c;
            minus_d = d;
        } else {
            const float half_a = 0.5 * size_a;
            const float half_b = 0.5 * size_b;
            const float half_c = 0.5 * size_c;
            const float half_d = 0.5 * size_d;
            minus_a = fma(-dir_a, vec3(half_a), a);
            minus_b = fma(-dir_b, vec3(half_b), b);
            minus_c = fma(-dir_c, vec3(half_c), c);
            minus_d = fma(-dir_d, vec3(half_d), d);
            plus_a = fma(dir_a, vec3(half_a), a);
            plus_b = fma(dir_b, vec3(half_b), b);
            plus_c = fma(dir_c, vec3(half_c), c);
            plus_d = fma(dir_d, vec3(half_d), d);
        }
        // The maps' v at this particle and at the next (both at the window's top where map 0
        // runs a flipbook on time or along the ribbon).
        const uint map0_mode = bitfieldExtract(MaterialWord(material, 120u), 6, 2);
        const bool flat_v = map0_mode == 0u || map0_mode == 2u;
        const float k_c = flat_v ? 0.0 : this_t;
        const float k_b = flat_v ? 1.0 : next_t;
        vec4 v_c;
        vec4 v_b;
        for (uint map = 0u; map < 4u; map++) {
            v_c[map] = fma(windows[map].w - windows[map].y, k_c, windows[map].y);
            v_b[map] = fma(windows[map].w - windows[map].y, k_b, windows[map].y);
        }
        vec3 position = vec3(0.0);
        bool far_u = (vertex & 1u) != 0u;
        vec4 v_at = v_c;
        float along = 0.0;
        if (vertex == 0u) {
            position = plus_c;
            color = color_c;
        } else if (vertex == 1u) {
            position = minus_c;
            angle = GameAtan2(minus_c.x, minus_c.y);
            color = color_c;
        } else if (vertex < 8u) {
            along = 0.25 * float(vertex >> 1);
            const float u2 = along * along;
            const float u3 = along * u2;
            const vec3 plus = RibbonSidePoint(plus_d, plus_c, plus_b, plus_a, along);
            const vec3 minus = RibbonSidePoint(minus_d, minus_c, minus_b, minus_a, along);
            const vec3 width = plus - minus;
            const float k = (1.0 / sqrt(max(uintBitsToFloat(0x34000000u),
                                            width.z * width.z +
                                                (width.y * width.y + width.x * width.x)))) *
                            RibbonSize(size_d, size_c, size_b, size_a, along, u2, u3);
            if (far_u) {
                // The game's program rounds the z lane's product before the add.
                const vec3 half_sum = (plus + minus) * 0.5;
                position = vec3(width.xy * -k + half_sum.xy,
                                RibbonUnfused(width.z, -k, half_sum.z));
                angle = RibbonAtan2(position.x, position.y);
            } else {
                position = 0.5 * (plus + minus) + k * width;
            }
            // The game's program blends the colour on the plus side only; the minus side keeps
            // this particle's.
            const vec4 blended =
                vec4(RibbonBlend(color_d.r, this_factor.r, color_c.r, color_b.r, color_a.r,
                                 next_factor.r, along, u2, u3),
                     RibbonBlend(color_d.g, this_factor.g, color_c.g, color_b.g, color_a.g,
                                 next_factor.g, along, u2, u3),
                     RibbonBlend(color_d.b, this_factor.b, color_c.b, color_b.b, color_a.b,
                                 next_factor.b, along, u2, u3),
                     RibbonBlend(color_d.a, this_factor.a, color_c.a, color_b.a, color_a.a,
                                 next_factor.a, along, u2, u3));
            color = far_u ? color_c : blended;
            v_at = fma(vec4(along), v_b - v_c, v_c);
        } else if (vertex == 8u) {
            position = plus_b;
            color = color_b;
            v_at = v_b;
        } else {
            // Vertex 9 and any past it.
            position = minus_b;
            angle = GameAtan2(minus_b.x, minus_b.y);
            color = color_b;
            v_at = v_b;
            far_u = true;
        }
        {
            const vec3 view = in_view ? position : ViewPosition(position, ViewTranslation());
            pull = -DrawFloat(74u) * (1.0 / view.z) + 1.0;
            pulled = view * pull;
            // The view depth's negative, the negation taken into the product.
            depth = view.z * -pull;
            clip = ClipPosition(pulled);
            for (uint map = 0u; map < 4u; map++) {
                // Between the ends u is taken as fma(u - u, along, u), NaN for an infinite u.
                const float u = far_u ? windows[map].z : windows[map].x;
                const float u_at = vertex >= 2u && vertex < 8u ? fma(u - u, along, u) : u;
                maps[map] = vec4(u_at, v_at[map], far_u ? 1.0 : -1.0, 0.0);
            }
            color = ListedColor(chain.emitter, color);
            color = color * chain.color;
            shown = 1.0;
            t = chain.t;
            records = uvec4(chain.effect, chain.emitter, chain.description, chain.particle);
            set_bits = uvec4(chain.set, chain.set, chain.set + 0x8000u, 0u);
        }
    }
    attr10 = uintBitsToFloat(set_bits);
    attr9 = uintBitsToFloat(records);
    attr8 = vec4(pulled, shown);
    attr7 = vec4(0.0);
    attr6 = vec4(0.0, 0.0, 0.0, t);
    attr5 = maps[3];
    attr4 = maps[2];
    attr3 = maps[1];
    attr2 = maps[0];
    attr1 = vec4(depth, angle, 0.0, 5.0 * pull);
    attr0 = color;
    return clip;
}
