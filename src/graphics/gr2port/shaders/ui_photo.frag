// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's UI program 0x1b72c8c, the photo's filter: the photo in the first texture blurred by
// a Gaussian over 33 x 33 reads, put through the colour curves, with its brightness, saturation
// and film grain set by the distance from the middle, under the filter's frame in the second
// texture. The constants are 16-byte rows: rows 0-255 hold the curves by a channel's 8-bit value,
// with the brightness curve in w, and rows 256-511 the settings by distance and the grain by
// brightness.
#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_samplerless_texture_functions : require

#include "ui_pixel.glsl"

// Location 1: the photo's coordinates, then the frame's.
layout(location = 1) in vec4 attr0;
layout(location = 0) out vec4 color0;

// Scene::UiDrawPreview.
const uint DrawPreview = 64u;

// The game's conversion to an integer: 0 for NaN, and saturating.
uint ToInt(float value) {
    return isnan(value) ? 0u : uint(int(clamp(value, -2147483648.0, 2147483520.0)));
}

// Word `word` of constant row `row`, with the word's index formed in 32 bits as the game's
// program forms it.
float Row(UiConstants constants, uint row, uint word) {
    // The constants hold 512 rows; a row past them reads 0, as the game's buffer load gives.
    const float value = constants.constants[((min(row, 511u) * 16u) >> 2) + word];
    return row < 512u ? value : 0.0;
}

float K(uint bits) {
    return uintBitsToFloat(bits);
}

// The most texels the viewfinder's blur fetches along each axis.
const int SpanTexels = 16;

// The viewfinder's blur: the photo's sum, which weights each read by a factor per axis and mixes
// the two nearest texels per axis in each read. So it is a sum of texels, each weighted by a
// column weight times a row weight, and fetching each texel once replaces the 1089 reads. False
// when the reads span more than SpanTexels texels along an axis.
bool PreviewBlur(uvec2 first, vec2 photo, float radius, out vec4 blurred) {
    // Without a radius each read takes the same place.
    if (radius == 0.0) {
        blurred = clamp(Sample(first, photo), 0.0, 1.0);
        return true;
    }
    const uint slot = first.x & TextureSlotMask;
    const ivec2 size = textureSize(textures[nonuniformEXT(slot)], 0);
    const vec2 step = radius * K(0x3a800000u) * vec2(size);
    const vec2 middle = photo * vec2(size) - 0.5;
    const ivec2 low = ivec2(floor(middle - 16.0 * step));
    const ivec2 span = ivec2(floor(middle + 16.0 * step)) - low + 2;
    if (span.x > SpanTexels || span.y > SpanTexels) {
        return false;
    }
    float columns[SpanTexels];
    float rows[SpanTexels];
    for (int k = 0; k < SpanTexels; k++) {
        columns[k] = 0.0;
        rows[k] = 0.0;
    }
    float total = 0.0;
    for (int i = -16; i <= 16; i++) {
        const float weight = exp2(K(0x3bb8aa3bu) * float(-(i * i)));
        const vec2 at = middle + float(i) * step;
        const vec2 cell = floor(at);
        const vec2 part = at - cell;
        const ivec2 index = ivec2(cell) - low;
        columns[index.x] += weight * (1.0 - part.x);
        columns[index.x + 1] += weight * part.x;
        rows[index.y] += weight * (1.0 - part.y);
        rows[index.y + 1] += weight * part.y;
        total += weight;
    }
    vec4 sum = vec4(0.0);
    for (int y = 0; y < span.y; y++) {
        vec4 line = vec4(0.0);
        for (int x = 0; x < span.x; x++) {
            // The shot's sampler wraps.
            const ivec2 texel = low + ivec2(x, y);
            const ivec2 wrapped = texel - size * ivec2(floor(vec2(texel) / vec2(size)));
            line += texelFetch(textures[nonuniformEXT(slot)], wrapped, 0) * columns[x];
        }
        sum += line * rows[y];
    }
    blurred = clamp(sum / (total * total), 0.0, 1.0);
    return true;
}

void main() {
    UiDraw draw = Draw();
    UiConstants constants = PixelConstants(draw);
    const vec2 photo = attr0.xy;
    const vec2 frame = attr0.zw;

    // The settings row by distance from the middle.
    const float across = 2.0 * frame.x + -1.0;
    const float down = -2.0 * frame.y + 1.0;
    const float distance = sqrt(down * down + across * across);
    const uint settings = ToInt(255.0 * min(1.0, K(0x3f3504f3u) * distance)) + 256u;
    const float brightness = Row(constants, settings, 0u);
    const float saturation = Row(constants, settings, 1u);
    const float radius = Row(constants, settings, 2u);

    // The blur: weights e^(-(i^2 + j^2) / 256) over offsets i, j of -16..16 times radius / 1024.
    const uvec2 first = FirstTexture(draw);
    vec4 blurred;
    if ((draw.flags & DrawPreview) == 0u || !PreviewBlur(first, photo, radius, blurred)) {
        vec4 sum = vec4(0.0);
        float total = 0.0;
        for (int i = -16; i <= 16; i++) {
            const float x = float(i) * radius * K(0x3a800000u) + photo.x;
            for (int j = -16; j <= 16; j++) {
                const float y = float(j) * radius * K(0x3a800000u) + photo.y;
                const vec4 texel = Sample(first, vec2(x, y));
                const float weight = exp2(K(0x3bb8aa3bu) * float(-(i * i + j * j)));
                total = weight + total;
                sum = texel * weight + sum;
            }
        }
        const float inverse = 1.0 / total;
        blurred = clamp(inverse * sum, 0.0, 1.0);
    }

    // The curves.
    const float red = Row(constants, ToInt(255.0 * blurred.r), 0u);
    const float green = Row(constants, ToInt(255.0 * blurred.g), 1u);
    const float blue = Row(constants, ToInt(255.0 * blurred.b), 2u);

    // The grain: a hash of the frame's place. The viewfinder's view leaves the filter's frame out:
    // only the photo gets it.
    const vec4 overlay =
        (draw.flags & DrawPreview) != 0u ? vec4(0.0) : Sample(SecondTexture(draw), frame);
    const float seed = 256.0 * (frame.y * K(0x402a91b9u) + K(0x41b92023u) * frame.x) +
                       K(0x33d6bf95u);
    const float grain = fract(fract(abs(K(0x4ceb79a3u) * (1.0 / seed))) * abs(seed));

    // Brightness and saturation through luma and colour difference.
    const float luma = blue * K(0x3de978d5u) + (green * K(0x3f1645a2u) + K(0x3e991687u) * red);
    const float to_blue = 0.5 * blue + (green * K(0xbea99ae9u) + K(0xbe2cca2eu) * red);
    const float to_red = blue * K(0xbda685dbu) + (green * K(0xbed65e89u) + 0.5 * red);
    const float scaled_blue = to_blue * (2.0 * saturation);
    const float scaled_red = to_red * (2.0 * saturation);
    const float bright = brightness * Row(constants, ToInt(255.0 * luma), 3u);
    const float strength = Row(constants, ToInt(255.0 * bright) + 256u, 3u);
    const float out_red = scaled_red * K(0x3fb374bcu) + bright;
    const float out_green = K(0x3f36d1e1u) * -scaled_red + (K(0x3eb0331eu) * -scaled_blue + bright);
    const float out_blue = scaled_blue * K(0x3fe2d0e5u) + bright;
    const float grained = strength * (-1.0 + grain) + 1.0;
    const vec3 color = vec3(out_red, out_green, out_blue);
    const vec3 shown = overlay.a * (grained * -color + overlay.rgb) + color * grained;
    const float alpha = (overlay.a - blurred.a) * overlay.a + blurred.a;
    color0 = Export(vec4(shown, alpha));
}
