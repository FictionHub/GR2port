// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The records of the GPU particle pools and tables the game makes. particles.cpp makes the pools at
// least this large. The programs read a record past them as 0, as the game's buffer loads past
// their records give, and drop a write to one.
const uint ParticleCount = 163840u;
const uint EmitterCount = 98304u;
const uint DescriptionCount = 0x8000u;
const uint EffectCount = 0x600u;
const uint MaterialCount = 0x10000u;

// The records of the pool or table whose records take `size` bytes: particles, emitters,
// effects, descriptions or materials.
uint RecordsOfSize(uint size) {
    return size == 64u    ? ParticleCount
           : size == 128u ? EmitterCount
           : size == 192u ? EffectCount
           : size == 256u ? DescriptionCount
                          : MaterialCount;
}
