// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The texture array's slot mask: its slot count, a power of two, less one. pipeline.cpp sets it for
// the GPU. A slot number past the array then names a slot inside it, and every slot holds a texture
// (white until one is installed), so no read leaves the array.
#ifndef TEXTURE_SLOTS
#define TEXTURE_SLOTS
layout(constant_id = 100) const uint TextureSlotMask = 0xffffu;
#endif
