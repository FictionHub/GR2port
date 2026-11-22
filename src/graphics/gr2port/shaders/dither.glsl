// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The noise of the game's dithered alpha, from 0 to 1: a hash of the pixel's place and depth. The
// game's programs discard a pixel whose alpha is below its noise.
float DitherNoise(vec2 position, float depth) {
    float seed = uintBitsToFloat(0x3d9df94au) * position.x;
    seed = position.y * uintBitsToFloat(0x3c118d6bu) + seed;
    seed = depth * seed;
    seed = 256.0 * seed + uintBitsToFloat(0x33d6bf95u);
    const float noise = fract(abs(uintBitsToFloat(0x4ceb79a3u) * (1.0 / seed)));
    return fract(noise * abs(seed));
}
