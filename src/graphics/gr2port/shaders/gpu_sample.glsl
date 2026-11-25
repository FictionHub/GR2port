// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the game's sample mesh compute programs share: the push constants, the words of the sample's
// constants, the effect objects, the descriptions and the particles they read, the game's integer
// conversions, the turn by the object's rotation and the vertex's normal.

#include "particle_pools.glsl"

layout(buffer_reference, std430, buffer_reference_align = 4) buffer Words {
    uint words[];
};
layout(push_constant) uniform Push {
    uint64_t vertices_address;
    uint64_t description_address;
    uint64_t particle_out_address;
    uint64_t emitter_out_address;
    uint64_t constants_address;
    // The skinned mesh's skin stream and palette.
    uint64_t weights_address;
    uint64_t palette_address;
};

// Scene::ParticleSample's constants: the mesh's 3x4 matrix in rows, its vertex count, its stride
// and its skin stream's, where in a vertex its position and normal lie and in the skin stream its
// weights and bones, the effect object, and the mode.
const uint SampleMatrix = 0u;
const uint SampleVertices = 12u;
const uint SampleStride = 16u;
const uint SampleSkinStride = 17u;
const uint SamplePosition = 20u;
const uint SampleNormal = 21u;
const uint SampleWeights = 22u;
const uint SampleBones = 23u;
const uint SampleObject = 24u;
const uint SampleMode = 25u;
const uint ModeNormals = 3u;

// An effect object (128 bytes): the scale at the start, its rotation, its description, 1 while it
// places particles, its first particle in the pool, the scale at the end, how long its placing
// takes, where in its pool it is, how many it places this frame, and the time its placing ends.
const uint ObjectScaleStart = 12u;
const uint ObjectRotation = 16u;
const uint ObjectDescription = 32u;
const uint ObjectPlacing = 40u;
const uint ObjectFirst = 48u;
const uint ObjectScaleEnd = 76u;
const uint ObjectDuration = 104u;
const uint ObjectCursor = 116u;
const uint ObjectCount = 120u;
const uint ObjectEnd = 124u;

// A description (256 bytes): 1 for a mesh's particles, its pool size, a 2 that divides the cursor
// by its own divisor, a 1 that leaves out the rotation and scale, the divisor, the position's and
// the normal's scales, whether it samples the mesh, and, above 0, picking vertices in turn.
const uint DescriptionMesh = 16u;
const uint DescriptionPool = 24u;
const uint DescriptionOwnDivisor = 68u;
const uint DescriptionFixed = 72u;
const uint DescriptionDivisor = 76u;
const uint DescriptionPositionScale = 80u;
const uint DescriptionNormalScale = 128u;
const uint DescriptionSamples = 200u;
const uint DescriptionInTurn = 216u;

// A particle (64 bytes): its position, its velocity, and the seed of its hash at word 13.
const uint ParticlePosition = 0u;
const uint ParticleVelocity = 16u;
const uint ParticleSeed = 52u;

uint Word(uint64_t records, uint record, uint size, uint byte_offset) {
    const uint count = RecordsOfSize(size);
    const uint at = (min(record, count - 1u) * size + byte_offset) >> 2;
    return record < count ? Words(records).words[at] : 0u;
}

float Float(uint64_t records, uint record, uint size, uint byte_offset) {
    return uintBitsToFloat(Word(records, record, size, byte_offset));
}

vec3 Vector(uint64_t records, uint record, uint size, uint byte_offset) {
    return vec3(Float(records, record, size, byte_offset),
                Float(records, record, size, byte_offset + 4u),
                Float(records, record, size, byte_offset + 8u));
}

void SetVector(uint64_t records, uint record, uint size, uint byte_offset, vec3 value) {
    if (record >= RecordsOfSize(size)) {
        return;
    }
    Words words = Words(records);
    const uint at = (record * size + byte_offset) >> 2;
    words.words[at] = floatBitsToUint(value.x);
    words.words[at + 1u] = floatBitsToUint(value.y);
    words.words[at + 2u] = floatBitsToUint(value.z);
}

// The game's conversion to an unsigned integer: NaN gives 0, other values are clamped to the uint
// range.
uint GameUint(float value) {
    return isnan(value) ? 0u : uint(clamp(value, 0.0, 4294967040.0));
}

// The remainder of a division as the game's program computes it, which leaves the dividend where
// the divisor is 0.
uint GameRemainder(uint dividend, uint divisor) {
    return divisor == 0u ? dividend : dividend % divisor;
}

// a turned by the quaternion q, with the roundings of the game's program: each sum of two products
// rounds one product and fuses the other, as the fma calls write out.
vec3 Turned(vec4 q, vec3 a) {
    const vec3 across = vec3(fma(q.y, a.z, a.y * -q.z), fma(q.z, a.x, a.z * -q.x),
                             fma(q.x, a.y, a.x * -q.y));
    const vec3 t = fma(vec3(q.w), a, across);
    const float d = fma(a.z, q.z, fma(a.y, q.y, q.x * a.x));
    return vec3(fma(q.x, d, fma(q.w, t.x, fma(t.z, q.y, -q.z * t.y))),
                fma(q.y, d, fma(q.w, t.y, fma(t.x, q.z, -q.x * t.z))),
                fma(q.z, d, fma(q.w, t.z, fma(q.x, t.y, -q.y * t.x))));
}

// A vertex's R11G11B10 normal, each component's exponent and mantissa moved into a half float's,
// taken from 0 to 1 to -1 to 1.
vec3 VertexNormal(uint word) {
    return 2.0 * vec3(unpackHalf2x16((word << 4) & 0x7ff0u).x,
                      unpackHalf2x16((word >> 7) & 0x7ff0u).x,
                      unpackHalf2x16((word >> 17) & 0x7fe0u).x) +
           -1.0;
}
