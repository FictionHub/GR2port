// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's UI program 0x1c01a44, the comic panels' ink strokes. For each triangle that
// ui_stroke.vert passes on, it draws two hand-drawn lines along the sides that meet at the corner
// with the strictly widest angle, so a panel drawn as two triangles is outlined without its
// diagonal. Each line is a quad of two triangles: its ends jittered by sums of cosines of a seed,
// pushed out along the line by wobbling amounts, and widened sideways. The constants, in the
// draw's palette words, are the matrix in 0-15, the width in 16 (0 draws nothing), the seed in 17,
// the jitter in 18 and the end length in 19. Each vertex passes the stroke pixel program its place
// along and across the line, the line's length and a phase.
#version 460
#extension GL_GOOGLE_include_directive : require

#include "ui_matrix.glsl"

layout(triangles) in;
layout(triangle_strip, max_vertices = 12) out;

// The export ring of each corner: its position in xyz and its colour.
layout(location = 0) in vec4 ring0[];
layout(location = 1) in vec4 ring1[];
layout(location = 15) flat in uint draw_index_in[];
layout(location = 15) flat out uint draw_index;
layout(location = 0) out vec4 attr0;
layout(location = 1) out vec4 attr1;

float K(uint bits) {
    return uintBitsToFloat(bits);
}

// The polynomial of the game's acos approximation at t, 1 + x or 1 - x, with signed_x the x it
// scales.
float AcosPolynomial(float signed_x, float t) {
    const float t2 = t * t;
    const float t3 = t * t2;
    float q = fma(signed_x, K(0xbb2ffdb3u), K(0xbb2ffdb3u));
    q = t3 * K(0xb9c90930u) + q;
    float r = fma(signed_x, K(0xbdf15befu), K(0xbdf15befu));
    const float s = K(0xb921c939u) * t2 + K(0xba8197e8u);
    q = K(0xbc014caeu) + q;
    r = t2 * K(0xbcd93924u) + r;
    r = K(0xbfb504f3u) + r;
    q = s * t2 + q;
    return q * t3 + r;
}

float GameAcos(float x) {
    if (0.0 >= x) {
        const float t = 1.0 + x;
        return sqrt(t) * AcosPolynomial(x, t) + K(0x40490fdbu);
    }
    const float t = 1.0 - x;
    return (1.0 / -(1.0 / sqrt(t))) * AcosPolynomial(-x, t);
}

// The angle at a corner between its edges to the next and the previous corner; false when an
// edge or the product of their lengths is 0.
bool CornerAngle(vec3 corner, vec3 next, vec3 previous, out float angle) {
    const vec3 first = next - corner;
    const vec3 second = previous - corner;
    const float first_length = sqrt(first.z * first.z + (first.y * first.y + first.x * first.x));
    const float second_length =
        sqrt(second.z * second.z + (second.y * second.y + second.x * second.x));
    angle = 0.0;
    if (!(second_length != 0.0 && first_length != 0.0)) {
        return false;
    }
    const float lengths = first_length * second_length;
    if (!(lengths != 0.0)) {
        return false;
    }
    const float dot_edges = second.z * first.z + (second.y * first.y + first.x * second.x);
    angle = GameAcos((1.0 / lengths) * dot_edges);
    return true;
}

float Wave(float turns) {
    return cos(6.283185307 * fract(abs(turns)));
}

// A sum of three cosines of the seed at the game's three rates and the given phases.
float Wobble(float seed, uint phase_a, uint phase_b, uint phase_c) {
    return (Wave(fma(seed, K(0x3ad09b89u), K(phase_a))) +
            Wave(fma(seed, K(0x3badd6f3u), K(phase_b)))) +
           Wave(fma(seed, K(0x3b3da4abu), K(phase_c)));
}

float LengthOf(vec3 vector) {
    return sqrt(vector.z * vector.z + (vector.y * vector.y + vector.x * vector.x));
}

// The end moved amount times width along the direction. Each component's two scalar products
// stay apart, as in the game's program; on a vector, the driver joins them, which rounds
// differently.
vec3 PushOut(vec3 end, vec3 direction, float width, float amount) {
    return vec3(amount * (width * direction.x) + end.x, amount * (width * direction.y) + end.y,
                amount * (width * direction.z) + end.z);
}

void EmitPoint(UiConstants constants, vec4 colour, float along, float across, float length,
               float phase, vec3 place) {
    gl_Position = TransformPosition(constants, place);
    attr0 = colour;
    attr1 = vec4(along, across, length, phase);
    draw_index = draw_index_in[0];
    EmitVertex();
}

// The two lines from corner a, along the sides to corners b and c.
void Strokes(UiConstants constants, vec3 a, vec3 b, vec3 c, vec4 colour_a, vec4 colour_b,
             vec4 colour_c) {
    const float width = constants.constants[16];
    const float jitter = constants.constants[18];
    const float seed = c.x > a.x ? 103.0 + constants.constants[17] : constants.constants[17];

    // The first line's frame: u from a to b, v across toward c.
    const vec3 to_b = b - a;
    const vec3 u = (1.0 / LengthOf(to_b)) * to_b;
    const vec3 to_c = c - a;
    const float along_c = u.z * to_c.z + (to_c.y * u.y + u.x * to_c.x);
    const vec3 off_line = along_c * -u + to_c;
    const vec3 v = (1.0 / LengthOf(off_line)) * off_line;

    const float w1 = (Wave(K(0x3ad09b89u) * seed) +
                      Wave(fma(seed, K(0x3badd6f3u), K(0x3db8b462u)))) +
                     Wave(fma(seed, K(0x3b3da4abu), K(0x3d6d0dd6u)));
    const float w2 = Wobble(seed, 0x3c8f6aeeu, 0x3e181c15u, 0x3db7b786u);
    const float w3 = Wobble(seed, 0x3e8b57e5u, 0x3f7f53b5u, 0x3f0d7ddcu);
    const float w4 = Wobble(seed, 0x3ebddd90u, 0x3fa9c3e9u, 0x3f3b6bbdu);
    const float w5 = Wobble(seed, 0x3ef546e0u, 0x3fd7f102u, 0x3f6dcb7au);
    const float w6 = Wobble(seed, 0x3edb336fu, 0x3fc23623u, 0x3f5616e5u);
    const float w7 = Wobble(seed, 0x3f21c09au, 0x400c90ceu, 0x3f9a749du);
    const float w8 = Wobble(seed, 0x3dacc0cdu, 0x3ebe231au, 0x3e584fd3u);
    const float w9 = Wobble(seed, 0x3e3eae2bu, 0x3f35fd06u, 0x3ecafa3fu);
    const float w10 = Wobble(seed, 0x3efd6cf3u, 0x3fdebb67u, 0x3f7533e9u);
    const float w11 = Wobble(seed, 0x3f053b56u, 0x3fe998d7u, 0x3f80871au);
    const float w12 = Wobble(seed, 0x3f2361d1u, 0x400dec7cu, 0x3f9befe7u);
    const float end_wobble = K(0x3d99999au) * constants.constants[19];
    const float end_base = 0.5 * constants.constants[19];

    // The first line, from a to b.
    const vec3 a1 = jitter * (w2 * v + u * w1) + a;
    const vec3 b1 = jitter * (w4 * v + -w3 * u) + b;
    const vec3 d1 = b1 - a1;
    const float length_d1 = LengthOf(d1);
    const vec3 dir1 = (1.0 / length_d1) * d1;
    const vec3 b_end = PushOut(b1, dir1, width, end_wobble * w5 + end_base);
    const vec3 a_end = (width * -(end_wobble * w6 + end_base)) * dir1 + a1;
    const float length1 = LengthOf(a_end - b_end);
    const float phase1 = 1000.0 * w7;
    const vec3 c1 = jitter * (w9 * -v + u * w8) + c;
    const vec3 to_c1 = c1 - a1;
    const float length_c1 = LengthOf(to_c1);
    const vec3 u2 = (1.0 / length_c1) * to_c1;
    const float cos12 = u2.z * dir1.z + (dir1.y * u2.y + dir1.x * u2.x);
    const vec3 side1 = (length_c1 * cos12) * -dir1 + to_c1;
    const vec3 e1 = (width * (1.0 / LengthOf(side1))) * side1;
    EmitPoint(constants, colour_a, 0.0, 0.0, length1, phase1, 0.5 * e1 + a_end);
    EmitPoint(constants, colour_a, 0.0, width, length1, phase1, -0.5 * e1 + a_end);
    EmitPoint(constants, colour_b, length1, 0.0, length1, phase1, 0.5 * e1 + b_end);
    EndPrimitive();
    EmitPoint(constants, colour_b, length1, 0.0, length1, phase1, 0.5 * e1 + b_end);
    EmitPoint(constants, colour_a, 0.0, width, length1, phase1, -0.5 * e1 + a_end);
    EmitPoint(constants, colour_b, length1, width, length1, phase1, -0.5 * e1 + b_end);
    EndPrimitive();

    // The second line, from a to c, in a frame from the first line's ends: u2 toward c, v2
    // across toward b.
    const vec3 off_line2 = -u2 * (length_d1 * cos12) + d1;
    const vec3 v2 = (1.0 / LengthOf(off_line2)) * off_line2;
    const vec3 c2 = jitter * (-w9 * u2 + w8 * v2) + c;
    const vec3 a2 = jitter * (w2 * u2 + w1 * v2) + a;
    const vec3 b2 = jitter * (w4 * u2 + -w3 * v2) + b;
    const vec3 d2 = c2 - a2;
    const vec3 dir2 = (1.0 / LengthOf(d2)) * d2;
    const vec3 c_end = PushOut(c2, dir2, width, end_wobble * w11 + end_base);
    const vec3 a_end2 = (width * -(end_wobble * w10 + end_base)) * dir2 + a2;
    const float length2 = LengthOf(a_end2 - c_end);
    const float phase2 = 1000.0 * w12;
    const vec3 to_b2 = b2 - a2;
    const float along_b2 = to_b2.z * dir2.z + (to_b2.y * dir2.y + dir2.x * to_b2.x);
    const vec3 side2 = along_b2 * -dir2 + to_b2;
    const vec3 e2 = (width * (1.0 / LengthOf(side2))) * side2;
    EmitPoint(constants, colour_a, 0.0, 0.0, length2, phase2, 0.5 * e2 + a_end2);
    EmitPoint(constants, colour_a, 0.0, width, length2, phase2, -0.5 * e2 + a_end2);
    EmitPoint(constants, colour_c, length2, 0.0, length2, phase2, 0.5 * e2 + c_end);
    EndPrimitive();
    EmitPoint(constants, colour_c, length2, 0.0, length2, phase2, 0.5 * e2 + c_end);
    EmitPoint(constants, colour_a, 0.0, width, length2, phase2, -0.5 * e2 + a_end2);
    EmitPoint(constants, colour_c, length2, width, length2, phase2, -0.5 * e2 + c_end);
    EndPrimitive();
}

// Corner `index`'s place and colour, picked by selects: an array read at a runtime index puts the
// corners in scratch memory on AMD's Windows driver.
vec3 CornerPoint(vec3 points[3], uint index) {
    return index == 0u ? points[0] : (index == 1u ? points[1] : points[2]);
}

vec4 CornerColour(uint index) {
    return index == 0u ? ring1[0] : (index == 1u ? ring1[1] : ring1[2]);
}

void main() {
    UiDraw draw = UiDraw(draws_address + uint64_t(draw_index_in[0]) * DrawBytes);
    UiConstants constants = UiConstants(draw.vertex_palette);
    if (!(constants.constants[16] != 0.0)) {
        return;
    }
    const vec3 points[3] = vec3[](ring0[0].xyz, ring0[1].xyz, ring0[2].xyz);
    float angles[3];
    if (!CornerAngle(points[0], points[1], points[2], angles[0]) ||
        !CornerAngle(points[1], points[2], points[0], angles[1]) ||
        !CornerAngle(points[2], points[0], points[1], angles[2])) {
        return;
    }
    uint a;
    if (angles[0] > max(angles[1], angles[2])) {
        a = 0u;
    } else if (angles[1] > max(angles[2], angles[0])) {
        a = 1u;
    } else if (angles[2] > max(angles[0], angles[1])) {
        a = 2u;
    } else {
        return;
    }
    const uint b = (a + 1u) % 3u;
    const uint c = (a + 2u) % 3u;
    Strokes(constants, CornerPoint(points, a), CornerPoint(points, b), CornerPoint(points, c),
            CornerColour(a), CornerColour(b), CornerColour(c));
}
