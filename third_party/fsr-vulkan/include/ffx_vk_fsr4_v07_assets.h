// SPDX-FileCopyrightText: Copyright 2026 GR2port
/*
 * File names of one FSR 4.1.2 Lite model set: the 10 pass modules of a network and size tier,
 * and the network's initializer. No filesystem or Vulkan calls.
 */
#pragma once

#include "ffx_vk_fsr4_v07_schedule.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FFX_FSR4_V07_ASSET_PATH_MAX 96u
#define FFX_FSR4_V07_INITIALIZER_BYTES 27392u

typedef struct FfxFsr4V07AssetSet {
    FfxFsr4ModelPreset preset;
    char tier[5];
    char pass[FFX_FSR4_PASS_COUNT][FFX_FSR4_V07_ASSET_PATH_MAX];
    char initializer[FFX_FSR4_V07_ASSET_PATH_MAX];
} FfxFsr4V07AssetSet;

/* The size tier of an output extent: "1080", "2160" or "4320". */
const char *ffxFsr4V07SelectAssetTier(uint32_t outputWidth,
                                      uint32_t outputHeight);

/*
 * The names of one model set. Returns false for an invalid preset, a zero or unsupported output
 * extent, or a null output.
 */
bool ffxFsr4V07BuildAssetSet(FfxFsr4ModelPreset preset,
                             uint32_t outputWidth,
                             uint32_t outputHeight,
                             FfxFsr4V07AssetSet *outAssets);

#ifdef __cplusplus
}
#endif
