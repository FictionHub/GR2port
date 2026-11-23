// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// What the nevi body's pixel programs share: Scene::EnemyDraw's pixel constants (Buffer1 in the
// programs' metadata) and mesh, the game's arc cosine, the ambient light of the world normal from
// the u_f4A, u_f4B and u_f4C terms, and the directional and point lights.

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer BodyConstants {
    float constants[124];
};
layout(buffer_reference, std430, buffer_reference_align = 8) readonly buffer EnemyMesh {
    uint64_t vertices;
    uint64_t skin;
    uint64_t palette;
    uint stride;
    uint displacement;
    uint elements[16];
    uint maps[8];
};
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer LightWords {
    uint words[];
};
const uint64_t EnemyDrawBytes = 896ul;
const uint64_t BodyPixelOffset = 272ul;
const uint64_t EnemyMeshOffset = 768ul;

// The mesh's maps: u_txNormal, u_txColorUV (the distortion of the other maps' coordinates),
// u_txColorAdd, u_txColorAdd2, u_txColorInner, and the boss's u_txNormalBoss and u_txMaskBoss.
const uint NormalMap = 0u;
const uint DistortionMap = 1u;
const uint AddMap = 2u;
const uint Add2Map = 3u;
const uint InnerMap = 4u;
const uint NormalBossMap = 5u;
const uint MaskBossMap = 6u;

// Buffer1's members, in words.
const uint LocalCamPos = 0u;
const uint ViewCamPos = 4u;
const uint TargetSize = 8u;
const uint Ar = 12u;
const uint Ag = 16u;
const uint Ab = 20u;
const uint Br = 24u;
const uint Bg = 28u;
const uint Bb = 32u;
const uint Cc = 36u;
const uint Flags = 40u;
const uint Translucency = 41u;
const uint DirLightRange = 42u;
const uint PntLight = 43u;
const uint ColorScaleNormal = 44u;
const uint ColorScaleInner = 45u;
const uint ColorScaleAdd = 46u;
const uint ColorScaleAdd2 = 47u;
const uint NormalUVOffset = 48u;
const uint NormalUVRepeat = 50u;
const uint UVUVOffset = 52u;
const uint UVUVRepeat = 54u;
const uint AddUVOffset = 56u;
const uint AddUVRepeat = 58u;
const uint Add2UVOffset = 60u;
const uint Add2UVRepeat = 62u;
const uint InnerUVOffset = 64u;
const uint InnerUVRepeat = 66u;
const uint FresnelColor = 68u;
const uint FresnelBorderL = 71u;
const uint SpecularScale = 72u;
const uint SpecularPower = 73u;
const uint FresnelAmbientRate = 74u;
const uint FresnelBorderH = 75u;
const uint LightDirection = 76u;
const uint DirLightColor = 80u;
const uint BaseColor = 84u;
const uint ColorScale = 87u;
const uint InverseZ = 88u;
const uint AlphaScaleInner = 90u;
const uint ParallaxDepth = 91u;
const uint ParallaxUVScale = 92u;
const uint ClipAngle = 93u;
const uint SmoothAngle = 94u;
const uint InnerDistortionScale = 95u;
const uint OuterDistortionScale = 96u;
const uint OuterDistortionScale2 = 97u;
const uint PunchThrough = 98u;
const uint SelfTranslucency = 100u;
const uint RenderTargetSizeInverse = 108u;
const uint NormalBossUVOffset = 112u;
const uint NormalBossUVRepeat = 114u;
const uint MaskBossUVOffset = 116u;
const uint MaskBossUVRepeat = 118u;
const uint AlphaRef = 120u;
const uint AlphaScale = 121u;
const uint ColorScaleNormalBoss = 122u;
const uint MaskDistortionScale = 123u;
// u_nFlags: bit 5 lights the body.
const uint FlagLit = 0x20u;

layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler texture_sampler;
layout(set = 0, binding = 2) uniform sampler clamp_sampler;
layout(set = 1, binding = 0) uniform texture2D self_translucent_color;
layout(push_constant) uniform Push {
    uint64_t draws_address;
    uint64_t point_lights_address;
    uint64_t light_grid_address;
};
layout(location = 15) flat in uint draw_index;

uint64_t BodyDrawAddress() {
    return draws_address + uint64_t(draw_index) * EnemyDrawBytes;
}

vec2 Pair(BodyConstants body, uint first) {
    return vec2(body.constants[first], body.constants[first + 1u]);
}

vec3 Triple(BodyConstants body, uint first) {
    return vec3(body.constants[first], body.constants[first + 1u], body.constants[first + 2u]);
}

vec4 Quad(BodyConstants body, uint first) {
    return vec4(body.constants[first], body.constants[first + 1u], body.constants[first + 2u],
                body.constants[first + 3u]);
}

// The vector over its length, in the order of operations of the game's programs.
vec3 Normalized(vec3 v) {
    return v * (1.0 / sqrt(v.z * v.z + (v.y * v.y + v.x * v.x)));
}

// The game's programs clear the lowest bit of a texture coordinate's x before some reads.
float LowBitCleared(float x) {
    return uintBitsToFloat(floatBitsToUint(x) & ~1u);
}

// The sign of the tangent's w as -1, 0 or 1.
float Handedness(float w) {
    return float((0.0 < w ? 1 : 0) - (0.0 > w ? 1 : 0));
}

// The game's arc cosine: a polynomial of 1 + x times its root, from pi, for x up to 0, and of
// 1 - x times its negated root above.
float GameAcos(float x) {
    float angle;
    if (0.0 >= x) {
        const float a = 1.0 + x;
        const float a2 = a * a;
        const float a3 = a * a2;
        float high = fma(x, uintBitsToFloat(0xbb2ffdb3u), uintBitsToFloat(0xbb2ffdb3u));
        high = a3 * uintBitsToFloat(0xb9c90930u) + high;
        float low = fma(x, uintBitsToFloat(0xbdf15befu), uintBitsToFloat(0xbdf15befu));
        high = uintBitsToFloat(0xbc014caeu) + high;
        const float middle = uintBitsToFloat(0xb921c939u) * a2 + uintBitsToFloat(0xba8197e8u);
        low = a2 * uintBitsToFloat(0xbcd93924u) + low;
        low = uintBitsToFloat(0xbfb504f3u) + low;
        high = middle * a2 + high;
        low = high * a3 + low;
        angle = sqrt(a) * low + uintBitsToFloat(0x40490fdbu);
    } else {
        const float b = 1.0 - x;
        const float b2 = b * b;
        const float b3 = b * b2;
        float high = fma(-x, uintBitsToFloat(0xbb2ffdb3u), uintBitsToFloat(0xbb2ffdb3u));
        high = b3 * uintBitsToFloat(0xb9c90930u) + high;
        float low = fma(-x, uintBitsToFloat(0xbdf15befu), uintBitsToFloat(0xbdf15befu));
        high = uintBitsToFloat(0xbc014caeu) + high;
        const float middle = uintBitsToFloat(0xb921c939u) * b2 + uintBitsToFloat(0xba8197e8u);
        low = b2 * uintBitsToFloat(0xbcd93924u) + low;
        low = uintBitsToFloat(0xbfb504f3u) + low;
        high = middle * b2 + high;
        const float root = 1.0 / -(1.0 / sqrt(b));
        low = high * b3 + low;
        angle = root * low;
    }
    return angle;
}

// The ambient light of the unit world normal n for one colour channel: the u_f4A term's x, y, z
// and w, the u_f4B term's xy, yz, zz and xz, and u_f4Cc's channel times x^2 - y^2.
float Ambient(vec4 a, vec4 b, float c, vec3 n, float xx_yy) {
    float sum = a.x * n.x + a.w;
    sum = a.y * n.y + sum;
    sum = a.z * n.z + sum;
    sum = b.x * (n.x * n.y) + sum;
    sum = b.y * (n.y * n.z) + sum;
    sum = b.z * (n.z * n.z) + sum;
    sum = b.w * (n.x * n.z) + sum;
    return c * xx_yy + sum;
}

// The distortion of the other maps' coordinates from -1 to 1: u_txColorUV's x and y at texture
// coordinates 3 through u_f2UVUVRepeat and u_f2UVUVOffset.
vec2 Distortion(BodyConstants body, uint handle, vec2 texcoord3) {
    const vec2 place =
        vec2(body.constants[UVUVRepeat] * texcoord3.x + body.constants[UVUVOffset],
             texcoord3.y * body.constants[UVUVRepeat + 1u] + body.constants[UVUVOffset + 1u]);
    return 2.0 * texture(sampler2D(textures[nonuniformEXT(handle)], texture_sampler),
                         vec2(LowBitCleared(place.x), place.y))
                     .xy +
           -1.0;
}

// The inner layer: its colour, its share of the body's colour and its map's alpha.
struct InnerLayer {
    vec3 color;
    float share;
    float alpha;
};

// The inner layer, where the camera is in front of the surface and the angle between the view
// and the mesh's normal is within u_fClipAngle: its map read from the point a plane
// u_fParallaxDepth times 100 behind the surface meets the view, in the tangent frame, shaded to
// u_f3BaseColor by the depth and faded out over u_fSmoothAngle with the vertex colour's alpha.
InnerLayer Inner(BodyConstants body, uint handle, vec3 normal, vec3 position, vec4 tangent_in,
                 float vertex_alpha, vec2 distortion, vec2 texcoord0) {
    InnerLayer inner = InnerLayer(vec3(0.0), 0.0, 0.0);
    const vec3 camera = Triple(body, LocalCamPos);
    const vec3 to_pixel = position - camera;
    const float facing = to_pixel.z * normal.z + (normal.y * to_pixel.y + to_pixel.x * normal.x);
    if (0.0 > facing) {
        const float cosine =
            -facing * (1.0 / sqrt(to_pixel.z * to_pixel.z +
                                  (to_pixel.y * to_pixel.y + to_pixel.x * to_pixel.x)));
        const float angle = GameAcos(cosine);
        const float clip = uintBitsToFloat(0x3c8efa35u) * body.constants[ClipAngle];
        if (angle < clip) {
            const float depth = body.constants[ParallaxDepth];
            const float plane =
                -depth * 100.0 +
                (position.z * normal.z + (normal.y * position.y + position.x * normal.x));
            const float camera_plane =
                camera.z * normal.z + (camera.y * normal.y + camera.x * normal.x);
            const float beyond = (1.0 / facing) * (plane - camera_plane) + -1.0;
            const vec3 offset = to_pixel * beyond;
            const float length_squared =
                offset.z * offset.z + (offset.y * offset.y + offset.x * offset.x);
            const vec3 tangent = Normalized(tangent_in.xyz);
            const vec3 direction = (1.0 / sqrt(length_squared)) * offset;
            const vec3 bitangent = vec3(tangent.y * -normal.z + normal.y * tangent.z,
                                        tangent.z * -normal.x + normal.z * tangent.x,
                                        tangent.x * -normal.y + normal.x * tangent.y);
            const float along_tangent =
                direction.x * tangent.x + (direction.y * tangent.y + tangent.z * direction.z);
            const float along_bitangent =
                Handedness(tangent_in.w) *
                (bitangent.x * direction.x +
                 (bitangent.y * direction.y + direction.z * bitangent.z));
            const float scale = body.constants[ParallaxUVScale] * sqrt(length_squared);
            const vec2 distorted = body.constants[InnerDistortionScale] * distortion + texcoord0;
            const vec2 place =
                Pair(body, InnerUVRepeat) * distorted +
                (vec2(along_tangent, along_bitangent) * scale + Pair(body, InnerUVOffset));
            const vec4 map =
                texture(sampler2D(textures[nonuniformEXT(handle)], texture_sampler),
                        vec2(LowBitCleared(place.x), place.y));
            const float smooth_start =
                uintBitsToFloat(0x3c8efa35u) * -body.constants[SmoothAngle] + clip;
            const float inverse = 1.0 / (clip - smooth_start);
            const float t = clamp(inverse * -smooth_start + angle * inverse, 0.0, 1.0);
            const float square = t * t;
            const float smooth_step = -2.0 * (t * square) + 3.0 * square;
            inner.share =
                body.constants[AlphaScaleInner] * (smooth_step * -vertex_alpha + vertex_alpha);
            const float inner_scale = body.constants[ColorScaleInner] * (1.0 - depth);
            inner.color = Triple(body, BaseColor) * depth + inner_scale * map.rgb;
            inner.alpha = map.a;
        }
    }
    return inner;
}

// The surface's normal in view space, not of unit length: a normal map from -1 to 1 over its
// length times the scale, turned by the view tangent, the view normal crossed with the tangent
// times the sign of the tangent's w, and the view normal.
vec3 ViewSurface(float scale, vec3 normal_map, vec4 view_tangent_in, vec3 view_normal_in) {
    const vec3 view_tangent = Normalized(view_tangent_in.xyz);
    const vec3 view_normal = Normalized(view_normal_in);
    const vec3 view_bitangent =
        vec3(view_tangent.y * -view_normal.z + view_normal.y * view_tangent.z,
             view_tangent.z * -view_normal.x + view_normal.z * view_tangent.x,
             view_tangent.x * -view_normal.y + view_normal.x * view_tangent.y) *
        Handedness(view_tangent_in.w);
    const vec3 map = 2.0 * normal_map + -1.0;
    const vec3 bent = (scale * (1.0 / sqrt(map.z * map.z + (map.y * map.y + map.x * map.x)))) * map;
    return view_normal * bent.z + (bent.y * view_bitangent + bent.x * view_tangent);
}

// The surface's colour before the lights: the ambient light of the world normal, moved towards
// u_f3FresnelColor by u_fFresnelAmbientRate, where the unit surface normal's z faces the view
// between u_fFresnelBorderL and u_fFresnelBorderH, and u_f3BaseColor times u_fColorScale
// elsewhere.
vec3 SurfaceColor(BodyConstants body, vec3 world_normal, float surface_z) {
    const float xx_yy = world_normal.y * -world_normal.y + world_normal.x * world_normal.x;
    const vec3 ambient = vec3(Ambient(Quad(body, Ar), Quad(body, Br), body.constants[Cc],
                                      world_normal, xx_yy),
                              Ambient(Quad(body, Ag), Quad(body, Bg), body.constants[Cc + 1u],
                                      world_normal, xx_yy),
                              Ambient(Quad(body, Ab), Quad(body, Bb), body.constants[Cc + 2u],
                                      world_normal, xx_yy));
    const float border = body.constants[FresnelBorderL];
    const float inverse_border = 1.0 / (body.constants[FresnelBorderH] - border);
    const float t = clamp(-border * inverse_border + abs(surface_z) * inverse_border, 0.0, 1.0);
    const float square = t * t;
    const float edge = -2.0 * (t * square) + 3.0 * square;
    const float rate = body.constants[FresnelAmbientRate];
    const vec3 fresnel = Triple(body, FresnelColor);
    const vec3 fresnel_ambient = fresnel + (-rate * fresnel + rate * ambient);
    const vec3 base = body.constants[ColorScale] * (1.0 - edge) *
                      clamp(max(vec3(0.0), Triple(body, BaseColor)), 0.0, 1.0);
    return edge * fresnel_ambient + base;
}

// The add maps' colours times u_fColorScaleAdd and u_fColorScaleAdd2, and the alpha the test
// against u_fAlphaRef takes: the largest of the add maps' and the inner map's alpha times
// u_fAlphaScale, at most 0.999.
struct AddLayer {
    vec3 color;
    float alpha;
};

AddLayer Added(BodyConstants body, uint add_handle, uint add2_handle, float inner_alpha,
               vec2 distortion, vec2 texcoord2, vec2 texcoord3) {
    const vec2 add2_place =
        Pair(body, Add2UVRepeat) *
            (body.constants[OuterDistortionScale2] * distortion + texcoord2) +
        Pair(body, Add2UVOffset);
    const vec2 add_place =
        Pair(body, AddUVRepeat) * (body.constants[OuterDistortionScale] * distortion + texcoord3) +
        Pair(body, AddUVOffset);
    const vec4 add2 = texture(sampler2D(textures[nonuniformEXT(add2_handle)], texture_sampler),
                              vec2(LowBitCleared(add2_place.x), add2_place.y));
    const vec4 add = texture(sampler2D(textures[nonuniformEXT(add_handle)], texture_sampler),
                             vec2(LowBitCleared(add_place.x), add_place.y));
    AddLayer added;
    added.alpha = min(uintBitsToFloat(0x3f7fbe77u),
                      body.constants[AlphaScale] * max(max(add2.a, inner_alpha), add.a));
    added.color =
        body.constants[ColorScaleAdd2] * add2.rgb + body.constants[ColorScaleAdd] * add.rgb;
    return added;
}

// The body's colour: the add maps outside the inner layer's share, the inner layer, all times the
// normal map's alpha, then the colour seen through the body by u_fSelfTranslucency and its alpha.
vec3 FinalColor(BodyConstants body, vec3 color, AddLayer added, InnerLayer inner,
                float normal_alpha) {
    color = added.color * (1.0 - inner.share) + color;
    color = inner.share * inner.color + color;
    color = normal_alpha * color;
    if (body.constants[SelfTranslucency] > 0.0) {
        const vec4 behind =
            texture(sampler2D(self_translucent_color, clamp_sampler),
                    vec2(body.constants[RenderTargetSizeInverse + 2u] * gl_FragCoord.x,
                         body.constants[RenderTargetSizeInverse + 3u] * gl_FragCoord.y));
        const float share = body.constants[SelfTranslucency] * behind.a;
        color = share * (behind.rgb - color) + color;
    }
    return color;
}

// The game's conversion to an unsigned integer: NaN gives 0, other values are clamped to the
// uint range.
uint GameUint(float value) {
    return isnan(value) ? 0u : uint(clamp(value, 0.0, 4294967040.0));
}

// A word of the point lights, 0 past their 12288 words.
uint LightWord(LightWords lights, uint word) {
    return word < 12288u ? lights.words[min(word, 12287u)] : 0u;
}

vec3 LightVector(LightWords lights, uint word) {
    return uintBitsToFloat(uvec3(LightWord(lights, word), LightWord(lights, word + 1u),
                                 LightWord(lights, word + 2u)));
}

// The colour lit by u_f3LightDirection's light and the point lights of the pixel's cell, for the
// unit surface normal s at the view position: each light's diffuse share is u_fTranslucency
// times the light's side of the surface plus its far side wrapped by u_fDirLightRange, and its
// specular u_fSpecularScale times the reflection against the view to u_fSpecularPower, the point
// lights' by their luminance. The cell is the screen's 60x36 tile in the slice of its view depth
// past the near plane, up to 48 lights a cell; a point light fades from its inner radius to its
// outer by its attenuation.
vec3 LitColor(BodyConstants body, vec3 color, vec3 s, vec3 view_position) {
    const vec3 to_camera = Triple(body, ViewCamPos) - view_position;
    const vec3 view =
        (1.0 / sqrt(to_camera.z * to_camera.z +
                    (to_camera.y * to_camera.y + to_camera.x * to_camera.x))) *
        to_camera;
    const vec3 light = Triple(body, LightDirection);
    const float n_dot_l = light.z * s.z + (light.y * s.y + light.x * s.x);
    const float twice = 2.0 * n_dot_l;
    float reflected = light.x * view.x;
    reflected = twice * (-s.x * view.x) + reflected;
    reflected = light.y * view.y + reflected;
    reflected = (-s.y * view.y) * twice + reflected;
    reflected = light.z * view.z + reflected;
    reflected = twice * (-s.z * view.z) + reflected;

    const float ndc = 2.0 * (1.0 - gl_FragCoord.z) + -1.0;
    const vec2 inverse_z = Pair(body, InverseZ);
    const float depth =
        1.0 / (inverse_z.x * ndc + inverse_z.y) - 1.0 / (inverse_z.y - inverse_z.x);
    const uint slice = uint(clamp(int(GameUint(floor(log2(depth)))), 0, 7));
    const uint row = GameUint(36.0 * (body.constants[TargetSize + 3u] * gl_FragCoord.y));
    const uint column = GameUint(60.0 * (body.constants[TargetSize + 2u] * gl_FragCoord.x));
    const uint first = 48u * (column + 60u * row + 2160u * slice);

    const float range = body.constants[DirLightRange];
    const float half_range = 0.5 * range;
    const float wrap = 0.5 * -range + 1.0;
    const float spec_power = body.constants[SpecularPower];
    const float translucency = body.constants[Translucency];
    float specular =
        body.constants[SpecularScale] * exp2(spec_power * log2(max(0.0, reflected)));
    const float back = max(0.0, wrap * -n_dot_l + half_range);
    vec3 diffuse = Triple(body, DirLightColor) * (translucency * max(0.0, n_dot_l) + back);

    LightWords grid = LightWords(light_grid_address);
    LightWords lights = LightWords(point_lights_address);
    const uint count = floatBitsToUint(body.constants[PntLight]);
    for (uint k = 0u; k < count; k++) {
        const uint at = (first + k) & 0x3fffffffu;
        const uint index = at < 829440u ? grid.words[min(at, 829439u)] : 0xffffffffu;
        if (index == 0xffffffffu) {
            break;
        }
        // A light is 48 bytes: f3Position, fOuterRadius, f3Color, fInnerRadius, fAttenuation.
        const uint bytes = 48u * index;
        const vec3 light_position = LightVector(lights, bytes >> 2);
        const float outer = uintBitsToFloat(LightWord(lights, (bytes + 12u) >> 2));
        const vec3 light_color = LightVector(lights, (bytes + 16u) >> 2);
        const float inner = uintBitsToFloat(LightWord(lights, (bytes + 28u) >> 2));
        const float attenuation = uintBitsToFloat(LightWord(lights, (bytes + 32u) >> 2));

        const vec3 to_light = light_position - view_position;
        const float distance_squared =
            to_light.z * to_light.z + (to_light.y * to_light.y + to_light.x * to_light.x);
        const vec3 l = (1.0 / sqrt(distance_squared)) * to_light;
        const float n_dot_light = l.z * s.z + (l.y * s.y + s.x * l.x);
        const vec3 r = -s * (-2.0 * n_dot_light) + -l;
        const float r_dot_v = view.z * r.z + (view.y * r.y + r.x * view.x);
        const float falloff =
            min(1.0, (1.0 / (outer - inner)) * max(0.0, sqrt(distance_squared) - inner));
        const float fade = 1.0 - exp2(attenuation * log2(abs(falloff)));
        const float luminance =
            light_color.b * uintBitsToFloat(0x3de978d5u) +
            (light_color.g * uintBitsToFloat(0x3f1645a2u) +
             uintBitsToFloat(0x3e991687u) * light_color.r);
        const float light_specular = (body.constants[SpecularScale] * fade) *
                                     exp2(spec_power * log2(max(0.0, r_dot_v)));
        const float light_back = translucency * max(0.0, -n_dot_light) +
                                 max(0.0, wrap * n_dot_light + half_range);
        specular = luminance * light_specular + specular;
        diffuse = fma(vec3(light_back), light_color * fade, diffuse);
    }
    return max(vec3(0.0), diffuse) * (color + max(0.0, specular));
}
