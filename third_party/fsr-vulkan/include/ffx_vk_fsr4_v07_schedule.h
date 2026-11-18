// SPDX-FileCopyrightText: Copyright 2026 GR2port
/*
 * Dispatch sizes, tensor sizes and scratch sizes of AMD's FSR 4.1.2 Lite network, as AMD's
 * provider computes them for an output extent.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FFX_FSR4_MODEL_PASS_COUNT 8u
#define FFX_FSR4_PASS_COUNT (FFX_FSR4_MODEL_PASS_COUNT + 2u) /* pre, model passes, post */
#define FFX_FSR4_TENSOR_COUNT 9u
#define FFX_FSR4_DOT4_MAX_OUTPUT_WIDTH 7680u
#define FFX_FSR4_DOT4_MAX_OUTPUT_HEIGHT 4320u

typedef struct FfxFsr4DispatchSize {
    uint32_t x;
    uint32_t y;
    uint32_t z;
} FfxFsr4DispatchSize;

typedef struct FfxFsr4Dot4Schedule {
    uint32_t alignedWidth;
    uint32_t alignedHeight;
    FfxFsr4DispatchSize pre;
    FfxFsr4DispatchSize model[FFX_FSR4_MODEL_PASS_COUNT];
    FfxFsr4DispatchSize post;
    /* The CsTensorSizes constant buffer: one uint4 (width, height, 0, 0) per tensor. */
    uint32_t tensorSizes[FFX_FSR4_TENSOR_COUNT][4];
} FfxFsr4Dot4Schedule;

typedef enum FfxFsr4ModelPreset {
    FFX_FSR4_MODEL_PRESET_NATIVE_AA = 0,
    FFX_FSR4_MODEL_PRESET_QUALITY,
    FFX_FSR4_MODEL_PRESET_BALANCED,
    FFX_FSR4_MODEL_PRESET_PERFORMANCE,
    FFX_FSR4_MODEL_PRESET_ULTRA_PERFORMANCE,
    FFX_FSR4_MODEL_PRESET_DRS
} FfxFsr4ModelPreset;

/*
 * Builds the schedule of the 10 passes for an output extent. Returns false for zero dimensions,
 * an extent beyond 7680x4320, or a null output pointer.
 */
bool ffxFsr4BuildDot4Schedule(uint32_t outputWidth,
                             uint32_t outputHeight,
                             FfxFsr4Dot4Schedule *outSchedule);

/* Activation scratch size of the size tier the output extent falls in. */
size_t ffxFsr4GetDot4ScratchSize(uint32_t maxOutputWidth,
                                uint32_t maxOutputHeight);

/* "main" for Native AA to Performance and DRS, "ultraperf" for Ultra Performance. */
const char *ffxFsr4ModelPresetName(FfxFsr4ModelPreset preset);

#ifdef __cplusplus
}
#endif
