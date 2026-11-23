// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The light of the game's lit particle pixel programs: the light's constants
// (Scene::ParticleLight), the directional light's wrapped term, and the point lights of the
// pixel's cell of LightCompute's grid. Define PARTICLE_PUSH_LIGHTS and include
// particle_gpu_push.glsl first.

// The light's constants: the directional light's colour (0 to 2), its direction (4 to 6), the
// depth's linear terms (10, 11) and the count of a cell's point lights to take (12).
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer LightConstants {
    float words[16];
};
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer LightGridWords {
    uint words[];
};

// The directional light's colour times its wrapped term at the cosine: (0.5 cosine + 0.5)^2 times
// 3 / (4 pi), with the cosine at least 0.
vec3 DirectionalDiffuse(LightConstants light, float cosine) {
    const float wrapped = 0.5 * max(0.0, cosine) + 0.5;
    return vec3(light.words[0], light.words[1], light.words[2]) *
           (uintBitsToFloat(0x3e747645u) * (wrapped * wrapped));
}

// The game's conversion to an unsigned integer: NaN gives 0, other values are clamped to the
// uint range.
uint LightUint(float value) {
    return isnan(value) ? 0u : uint(clamp(value, 0.0, 4294967040.0));
}

// The first light index of the pixel's cell: the screen's 60x36 tile at the place (0 to 1) in the
// slice of its view depth past the near plane, 48 indices a cell.
uint LightCellFirst(LightConstants light, vec2 place) {
    const float ndc = 2.0 * (1.0 - gl_FragCoord.z) + -1.0;
    const float depth = 1.0 / (light.words[10] * ndc + light.words[11]) -
                        1.0 / (light.words[11] - light.words[10]);
    const uint slice = uint(clamp(int(LightUint(floor(log2(depth)))), 0, 7));
    const uint row = LightUint(36.0 * place.y);
    const uint column = LightUint(60.0 * place.x);
    return 48u * (column + 60u * row + 2160u * slice);
}

// Light k of the cell, 0xffffffff past the grid's 829440 words; the cell's list ends at the first
// 0xffffffff.
uint CellLight(uint first, uint k) {
    const uint at = (first + k) & 0x3fffffffu;
    return at < 829440u ? LightGridWords(light_grid_address).words[min(at, 829439u)] : 0xffffffffu;
}

// A point light (48 bytes: f3Position, fOuterRadius, f3Color, fInnerRadius, fAttenuation), its
// words 0 past the lights' 12288 words.
struct PointLight {
    vec3 position;
    float outer;
    vec3 color;
    float inner;
    float attenuation;
};

uint PointLightWord(uint word) {
    return word < 12288u ? LightGridWords(point_lights_address).words[min(word, 12287u)] : 0u;
}

vec3 PointLightVector(uint word) {
    return uintBitsToFloat(
        uvec3(PointLightWord(word), PointLightWord(word + 1u), PointLightWord(word + 2u)));
}

PointLight ReadPointLight(uint index) {
    const uint bytes = 48u * index;
    PointLight light;
    light.position = PointLightVector(bytes >> 2);
    light.outer = uintBitsToFloat(PointLightWord((bytes + 12u) >> 2));
    light.color = PointLightVector((bytes + 16u) >> 2);
    light.inner = uintBitsToFloat(PointLightWord((bytes + 28u) >> 2));
    light.attenuation = uintBitsToFloat(PointLightWord((bytes + 32u) >> 2));
    return light;
}

// The part of a point light that fades at the distance: from 0 at its inner radius to 1 at its
// outer, to the power of its attenuation.
float PointLightFaded(PointLight light, float distance) {
    const float falloff =
        min(1.0, (1.0 / (light.outer - light.inner)) * max(0.0, distance - light.inner));
    return exp2(light.attenuation * log2(abs(falloff)));
}

// The diffuse light of a sprite's point lights: each light's direction turned into the sprite's
// frame by the rows, its colour times what is left of it past its fade, times the light's side of
// the normal n over pi.
vec3 SpritePointLights(LightConstants light, uint first, vec3 view_position, vec3 n, vec3 row0,
                       vec3 row1, vec3 row2, vec3 diffuse) {
    const uint count = floatBitsToUint(light.words[12]);
    for (uint k = 0u; k < count; k++) {
        const uint index = CellLight(first, k);
        if (index == 0xffffffffu) {
            break;
        }
        const PointLight point = ReadPointLight(index);
        const vec3 to_light = point.position - view_position;
        const float distance_squared =
            to_light.z * to_light.z + (to_light.y * to_light.y + to_light.x * to_light.x);
        const vec3 l = (1.0 / sqrt(distance_squared)) * to_light;
        const vec3 turned = vec3(row0.z * l.z + (row0.y * l.y + row0.x * l.x),
                                 row1.z * l.z + (row1.y * l.y + row1.x * l.x),
                                 row2.z * l.z + (row2.y * l.y + row2.x * l.x));
        const float faded = PointLightFaded(point, sqrt(distance_squared));
        const float facing = max(0.0, -(turned.z * n.z + (turned.y * n.y + n.x * turned.x)));
        const float lit = uintBitsToFloat(0x3ea2f983u) * (facing * -faded + facing);
        diffuse = lit * point.color + diffuse;
    }
    return diffuse;
}

// The diffuse light of a model's point lights: each light's colour times what is left of it past
// its fade, times the light's side of the view normal n over pi.
vec3 ModelPointLights(LightConstants light, uint first, vec3 view_position, vec3 n,
                      vec3 diffuse) {
    const uint count = floatBitsToUint(light.words[12]);
    for (uint k = 0u; k < count; k++) {
        const uint index = CellLight(first, k);
        if (index == 0xffffffffu) {
            break;
        }
        const PointLight point = ReadPointLight(index);
        const vec3 to_light = point.position - view_position;
        const float distance_squared =
            to_light.z * to_light.z + (to_light.y * to_light.y + to_light.x * to_light.x);
        const float faded = PointLightFaded(point, sqrt(distance_squared));
        const float facing =
            max(0.0, (1.0 / sqrt(distance_squared)) *
                         (to_light.z * n.z + (to_light.y * n.y + n.x * to_light.x)));
        const float lit = uintBitsToFloat(0x3ea2f983u) * (facing * -faded + facing);
        diffuse = lit * point.color + diffuse;
    }
    return diffuse;
}

// The sign of the tangent's w as -1, 0 or 1.
float Handedness(float w) {
    return float((0.0 < w ? 1 : 0) - (0.0 > w ? 1 : 0));
}
