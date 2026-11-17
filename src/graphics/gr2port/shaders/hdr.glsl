// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The HDR10 window's encoding: ST 2084 (PQ) of BT.2020 light.

// The inverse of the sRGB encoding.
vec3 DecodeSrgb(vec3 encoded) {
    return mix(pow((encoded + 0.055) / 1.055, vec3(2.4)), encoded / 12.92,
               lessThanEqual(encoded, vec3(0.04045)));
}

// Linear BT.709 light in BT.2020 primaries.
vec3 Bt2020(vec3 c) {
    return vec3(dot(c, vec3(0.6274040, 0.3292820, 0.0433136)),
                dot(c, vec3(0.0690970, 0.9195400, 0.0113612)),
                dot(c, vec3(0.0163916, 0.0880132, 0.8955950)));
}

// ST 2084 of light in units of 10000 nits.
vec3 Pq(vec3 light) {
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    const vec3 p = pow(clamp(light, 0.0, 1.0), vec3(m1));
    return pow((c1 + c2 * p) / (1.0 + c3 * p), vec3(m2));
}

// An sRGB-encoded colour as HDR10, its light times gain, with white at white over 10000 nits.
vec3 Hdr10(vec3 encoded, float gain, float white) {
    return Pq(Bt2020(DecodeSrgb(max(encoded, vec3(0.0))) * gain) * white);
}
