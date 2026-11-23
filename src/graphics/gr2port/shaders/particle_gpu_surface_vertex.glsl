// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the GPU particle surface vertex programs add past the model's point: the texture
// coordinates of the material's flipbook frame or scrolled window, the fog colour of a fade along
// the mesh's height from the vertex constants, and the emitter's entry in the colour list.
// Include particle_gpu_model_vertex.glsl first.

// The vertex constants (Scene::ParticleDraw at byte 464).
float VertexConstant(uint word) {
    return DrawFloat(116u + word);
}

// The game's unsigned division: all ones for a divisor of 0.
uint GameDivide(uint n, uint d) {
    return d != 0u ? n / d : 0xffffffffu;
}

// The texture coordinates at the mesh's (in 1/1024 units) within a window of the map. The
// window's time is the age t through the material's power curve (word 116: t^p from p = 0,
// 1 - (1 - t)^-p below), or with flag 2 of word 120 the time elapsed. With word 120's bits 6 and
// 7 the window scrolls: a random start in words 52 to 55 and 84 to 87 plus a random speed in 68
// to 71 and 100 to 103 times the time; else it is the frame of a flipbook of bits 8 to 15
// frames across and 16 to 23 down, the frame word 44 (plus with flag 1 a random share of all
// frames) plus word 48 times the time.
vec2 SurfaceCoordinates(ParticleModelPoint point, ivec2 texcoord) {
    const uint material = point.material;
    const float power = MaterialFloat(material, 116u);
    float curved;
    if (0.0 <= power) {
        curved = exp2(power * log2(abs(point.t)));
    } else {
        curved = 1.0 - exp2(-power * log2(abs(clamp(1.0 - point.t, 0.0, 1.0))));
    }
    const uint flags = MaterialWord(material, 120u);
    const float time = bitfieldExtract(flags, 2, 1) > 0u ? point.elapsed : curved;
    vec4 window;
    if (bitfieldExtract(flags, 6, 2) != 0u) {
        const float random = SeedRandom(point.seed + 0x1a8eu);
        const vec4 speed_low = MaterialVector(material, 68u);
        const vec4 start_low = MaterialVector(material, 52u);
        const vec4 speed = fma(vec4(random), MaterialVector(material, 100u) - speed_low, speed_low);
        const vec4 start = fma(vec4(random), MaterialVector(material, 84u) - start_low, start_low);
        window = fma(vec4(time), speed, start);
    } else {
        const uint across = bitfieldExtract(flags, 8, 8);
        const uint down = bitfieldExtract(flags, 16, 8);
        float first = 0.0;
        if (bitfieldExtract(flags, 1, 1) != 0u) {
            first = uintBitsToFloat(0x3ec01c14u) *
                    (SeedHash(point.seed + 0x1a8eu) * float(across * down));
        }
        const float to_across = 1.0 / float(across);
        const float to_down = 1.0 / float(down);
        const uint frame = GameUintOf(fma(time, MaterialFloat(material, 48u),
                                          MaterialFloat(material, 44u) + first));
        const uint line = GameDivide(frame, across);
        const uint column = frame - line * across;
        const uint row = line - GameDivide(line, down) * down;
        // The driver leaves the next frame's edges unfused.
        precise const float next_column = to_across * float(column) + to_across;
        precise const float next_row = float(row) * to_down + to_down;
        window = vec4(float(column) * to_across, float(row) * to_down, next_column, next_row);
    }
    const vec2 place = (1.0 / 1024.0) * vec2(texcoord);
    return vec2(fma(place.x, window.z - window.x, window.x),
                fma(place.y, window.w - window.y, window.y));
}

// The fog colour of a fade along the mesh's height: vertex constants 48 to 50 towards 52 to 54
// by clamp(c55 (height - c51)).
vec3 HeightFog(float height) {
    const float fade = clamp(fma(VertexConstant(55u), height,
                                 -VertexConstant(51u) * VertexConstant(55u)), 0.0, 1.0);
    const vec3 low = vec3(VertexConstant(48u), VertexConstant(49u), VertexConstant(50u));
    const vec3 high = vec3(VertexConstant(52u), VertexConstant(53u), VertexConstant(54u));
    return fma(high - low, vec3(fade), low);
}

// The parallax shift of a view position on a surface: its shares along the tangent and along
// the bitangent (cross(normal, tangent) times the tangent's w) over its share along the normal,
// times 0.1. Plain products and sums: the driver fuses them as it does the game's.
vec2 ParallaxShift(vec3 view, vec3 normal, vec4 tangent) {
    const float to_normal = 1.0 / (view.x * normal.x + (normal.y * view.y + view.z * normal.z));
    const vec3 bitangent = vec3(normal.z * -tangent.y + tangent.z * normal.y,
                                normal.x * -tangent.z + tangent.x * normal.z,
                                normal.y * -tangent.x + tangent.y * normal.x);
    const float along_tangent =
        tangent.x * view.x + (tangent.y * view.y + view.z * tangent.z);
    const float along_bitangent =
        view.x * bitangent.x + (view.y * bitangent.y + bitangent.z * view.z);
    return vec2(0.1 * (to_normal * along_tangent),
                0.1 * ((tangent.w * to_normal) * along_bitangent));
}

// The colour times the emitter's entry in the colour list (16 bytes an emitter) where
// c_materialAttr2.x reaches 1 - 2^-23.
vec4 ListedColor(ParticleModelPoint point, vec4 color) {
    if (DrawFloat(76u) >= uintBitsToFloat(0x3f7ffffeu)) {
        RecordWords list = RecordWords(color_address);
        const uint at = (point.emitter << 4) >> 2;
        const vec3 listed =
            uintBitsToFloat(uvec3(list.words[at], list.words[at + 1u], list.words[at + 2u]));
        color.rgb = listed * color.rgb;
    }
    return color;
}
