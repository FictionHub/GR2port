// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the game's GPU particle vertex programs share: Scene::ParticleDraw by gl_BaseInstance,
// the particle by gl_InstanceIndex, the particle's emitter, description, effect and material
// records, the game's hash of a particle's seed, the curves a random value spreads across, the
// quaternion turn and the view and projection rows. Define PARTICLE_PUSH_LIGHTS and include
// particle_gpu_push.glsl first.

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer RecordWords {
    uint words[];
};

layout(set = 1, binding = 0) uniform texture2D curve_texture;
layout(set = 1, binding = 1) uniform sampler curve_sampler;

layout(location = 15) flat out uint draw_index;

// Record sizes in bytes: the draw, a particle, an emitter, a description, an effect and a
// material.
const uint64_t ParticleDrawBytes = 1192ul;
const uint ParticleBytes = 64u;
const uint EmitterBytes = 128u;
const uint DescriptionBytes = 256u;
const uint EffectBytes = 192u;
const uint MaterialBytes = 560u;

// The draw's words (PfxBufferCommon: c_viewMat at 0, c_projMat at 16, c_clipPlane at 68,
// c_materialAttr at 72, c_index.x (the first particle) at 80, c_gpufxUsed.w (the first
// material) at 87), and the model block (Scene::ParticleDraw at byte 368).
RecordWords DrawWords() {
    return RecordWords(draws_address + uint64_t(draw_index) * ParticleDrawBytes);
}

RecordWords ModelWords() {
    return RecordWords(draws_address + uint64_t(draw_index) * ParticleDrawBytes + 368ul);
}

float DrawFloat(uint word) {
    return uintBitsToFloat(DrawWords().words[word]);
}

// Word `word` of record `index` of `size` bytes at `address`, as the game addresses it.
uint RecordWord(uint64_t address, uint index, uint size, uint word) {
    return RecordWords(address).words[(index * size + 4u * word) >> 2];
}

// Word `word` of record `index` of a table of `count`: 0 past the table, as the game's buffer
// loads past their records give.
uint TableWord(uint64_t address, uint index, uint count, uint size, uint word) {
    const uint value = RecordWord(address, min(index, count - 1u), size, word);
    return index < count ? value : 0u;
}

vec4 TableVector(uint64_t address, uint index, uint count, uint size, uint word) {
    return uintBitsToFloat(uvec4(TableWord(address, index, count, size, word),
                                 TableWord(address, index, count, size, word + 1u),
                                 TableWord(address, index, count, size, word + 2u),
                                 TableWord(address, index, count, size, word + 3u)));
}

uint ParticleWord(uint particle, uint word) {
    return TableWord(particle_address, particle, ParticleCount, ParticleBytes, word);
}

float ParticleFloat(uint particle, uint word) {
    return uintBitsToFloat(ParticleWord(particle, word));
}

vec4 ParticleVector(uint particle, uint word) {
    return TableVector(particle_address, particle, ParticleCount, ParticleBytes, word);
}

uint EmitterWord(uint emitter, uint word) {
    return TableWord(emitter_address, emitter, EmitterCount, EmitterBytes, word);
}

float EmitterFloat(uint emitter, uint word) {
    return uintBitsToFloat(EmitterWord(emitter, word));
}

vec4 EmitterVector(uint emitter, uint word) {
    return TableVector(emitter_address, emitter, EmitterCount, EmitterBytes, word);
}

uint DescriptionWord(uint description, uint word) {
    return TableWord(description_address, description, DescriptionCount, DescriptionBytes, word);
}

float DescriptionFloat(uint description, uint word) {
    return uintBitsToFloat(DescriptionWord(description, word));
}

uint EffectWord(uint effect, uint word) {
    return TableWord(effect_address, effect, EffectCount, EffectBytes, word);
}

uint MaterialWord(uint material, uint word) {
    return TableWord(material_address, material, MaterialCount, MaterialBytes, word);
}

float MaterialFloat(uint material, uint word) {
    return uintBitsToFloat(MaterialWord(material, word));
}

vec4 MaterialVector(uint material, uint word) {
    return TableVector(material_address, material, MaterialCount, MaterialBytes, word);
}

// The game's conversion to an unsigned integer: NaN gives 0, other values are clamped.
uint GameUintOf(float value) {
    return isnan(value) ? 0u : uint(clamp(value, 0.0, 4294967040.0));
}

// The game's hash of a seed, from 0 to 2.66514: fract(|0.00146566 n|) times 2.66514 of n, the
// seed's bits xor 0xc6395 as a float. Its sign is n / 256's, which a float of a uint never
// makes negative.
float SeedHash(uint seed) {
    const float n = float(0xc6395u ^ seed);
    const float hash =
        uintBitsToFloat(0x402a91b9u) * fract(abs(uintBitsToFloat(0x3ac01c14u) * n));
    return 0.0 > uintBitsToFloat(0x3b800000u) * n ? -hash : hash;
}

// A hash from 0 to 1: SeedHash times 0.375214.
float SeedRandom(uint seed) {
    return uintBitsToFloat(0x3ec01c14u) * SeedHash(seed);
}

// A curve at x from 0 to 1, between its column and the next by spread times the random value.
// The curve texture is 2048 texels wide: a curve's low 11 bits pick its column, the rest its
// block of 64 rows.
vec4 SpreadCurve(uint curve, float x, float spread, float random) {
    const uint column = curve & 0x7ffu;
    const float next = clamp((1.0 / 2048.0) * float(column + 1u) + 1.0 / 4096.0, 0.0, 1.0);
    const float first = clamp((1.0 / 2048.0) * float(column) + 1.0 / 4096.0, 0.0, 1.0);
    const float along = clamp(max(0.0, x), 0.0, 1.0) * 63.0 + float((curve >> 11) << 6);
    const float row = clamp((1.0 / 1024.0) * along + 1.0 / 2048.0, 0.0, 1.0);
    const float place = fma(spread * (next - first), random, first);
    return textureLod(sampler2D(curve_texture, curve_sampler), vec2(place, row), 0.0);
}

// The point p turned by the quaternion q (x, y, z, w), in the game's order of steps: of each
// two products the driver fuses the generated program's second into its add.
vec3 Turned(vec4 q, vec3 p) {
    const vec3 t = vec3(fma(q.w, p.x, fma(q.y, p.z, p.y * -q.z)),
                        fma(p.y, q.w, fma(q.z, p.x, -q.x * p.z)),
                        fma(p.z, q.w, fma(q.x, p.y, -q.y * p.x)));
    const float d = fma(q.z, p.z, fma(p.y, q.y, q.x * p.x));
    return vec3(fma(d, q.x, fma(t.x, q.w, fma(t.z, q.y, -q.z * t.y))),
                fma(d, q.y, fma(t.y, q.w, fma(q.z, t.x, -q.x * t.z))),
                fma(d, q.z, fma(t.z, q.w, fma(t.y, q.x, -q.y * t.x))));
}

// The quaternion of angles in degrees about x, then y, then z (half angles; cos = sin(a + pi/2)
// with the add left to the driver).
vec4 EulerTurn(vec3 degrees) {
    const float k = uintBitsToFloat(0x3c0efa35u);
    const float to_turns = uintBitsToFloat(0x3e22f983u);
    const float quarter = uintBitsToFloat(0x3fc90fdbu);
    const float cos_y = sin(6.283185307 * fract(to_turns * (k * degrees.y + quarter)));
    const float cos_x = sin(6.283185307 * fract(to_turns * (k * degrees.x + quarter)));
    const float sin_y = sin(6.283185307 * fract(to_turns * (k * degrees.y)));
    const float sin_x = sin(6.283185307 * fract(to_turns * (k * degrees.x)));
    const float cos_z = sin(6.283185307 * fract(to_turns * (k * degrees.z + quarter)));
    const float sin_z = sin(6.283185307 * fract(to_turns * (k * degrees.z)));
    const float cy_cx = cos_y * cos_x;
    const float cy_sx = cos_y * sin_x;
    const float sy_cx = sin_y * cos_x;
    const float sy_sx = sin_y * sin_x;
    return vec4(fma(sy_sx, sin_z, cos_z * cy_cx), fma(sy_cx, sin_z, cos_z * cy_sx),
                fma(cy_sx, sin_z, cos_z * sy_cx), fma(sy_sx, cos_z, sin_z * cy_cx));
}

// A row of c_viewMat or c_projMat (starting at `word`) applied to p: p's x, y and z one after
// another onto the given translation.
float MatrixRow(uint word, vec3 p, float translation) {
    return fma(DrawFloat(word + 2u), p.z,
               fma(DrawFloat(word + 1u), p.y, fma(DrawFloat(word), p.x, translation)));
}

// c_viewMat's translation.
vec3 ViewTranslation() {
    return vec3(DrawFloat(3u), DrawFloat(7u), DrawFloat(11u));
}

// The view position of p through c_viewMat's three rows onto the translation.
vec3 ViewPosition(vec3 p, vec3 translation) {
    return vec3(MatrixRow(0u, p, translation.x), MatrixRow(4u, p, translation.y),
                MatrixRow(8u, p, translation.z));
}

// The clip position of a view position through c_projMat's four rows.
vec4 ClipPosition(vec3 v) {
    return vec4(MatrixRow(16u, v, DrawFloat(19u)), MatrixRow(20u, v, DrawFloat(23u)),
                MatrixRow(24u, v, DrawFloat(27u)), MatrixRow(28u, v, DrawFloat(31u)));
}

// The depth's move: -c_materialAttr.z / c_clipPlane.z, twice in clip z.
float DepthOffset() {
    return -DrawFloat(74u) * (1.0 / DrawFloat(70u));
}

// GR2port's clip position from the game's: z from -w..w to 0..w.
vec4 VulkanClip(vec4 clip) {
    return vec4(clip.x, clip.y, 0.5 * (clip.z + clip.w), clip.w);
}
