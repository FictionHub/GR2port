// SPDX-FileCopyrightText: Copyright 2026 GR2port
#include "ffx_vk_fsr4_lite_schedule.h"

#include <string.h>

static uint32_t div_round_up(uint32_t value, uint32_t divisor)
{
    return value / divisor + (value % divisor != 0u);
}

static FfxFsr4DispatchSize dispatch_size(uint32_t x, uint32_t y)
{
    FfxFsr4DispatchSize result = {x, y, 1u};
    return result;
}

static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

/* The largest output of the size tier: the tensors are stored at the tier's size. */
static void tier_extent(uint32_t width, uint32_t height, uint32_t *tierWidth,
                        uint32_t *tierHeight)
{
    if (width <= 1920u && height <= 1080u) {
        *tierWidth = 1920u;
        *tierHeight = 1080u;
    } else if (width <= 3840u && height <= 2160u) {
        *tierWidth = 3840u;
        *tierHeight = 2160u;
    } else {
        *tierWidth = 7680u;
        *tierHeight = 4320u;
    }
}

/*
 * A post pass zeroes the border of a tensor stored at the tier's size divided by `divisor`, plus
 * one: one thread per border element, 32 threads per group.
 */
static FfxFsr4DispatchSize post_size(const uint32_t tensor[4], uint32_t tierWidth,
                                     uint32_t tierHeight, uint32_t divisor)
{
    const uint32_t w = tensor[0], h = tensor[1];
    const uint32_t padX = min_u32(tierWidth / divisor + 1u - w, 5u);
    const uint32_t padY = min_u32(tierHeight / divisor + 1u - h, 1u);
    const uint32_t row = w + 1u + padX;
    return dispatch_size(div_round_up(row + h + h * padX + row * padY, 32u), 1u);
}

bool ffxFsr4BuildDot4Schedule(uint32_t outputWidth,
                             uint32_t outputHeight,
                             FfxFsr4Dot4Schedule *outSchedule)
{
    static const uint32_t tensorDivisors[FFX_FSR4_TENSOR_COUNT] = {2, 1, 2, 4, 4, 2, 1, 1, 1};
    FfxFsr4Dot4Schedule schedule;
    uint32_t tierWidth, tierHeight, w, h;

    if (!outSchedule || !outputWidth || !outputHeight ||
        outputWidth > FFX_FSR4_DOT4_MAX_OUTPUT_WIDTH ||
        outputHeight > FFX_FSR4_DOT4_MAX_OUTPUT_HEIGHT)
        return false;

    memset(&schedule, 0, sizeof(schedule));
    w = schedule.alignedWidth = (outputWidth + 7u) & ~7u;
    h = schedule.alignedHeight = (outputHeight + 7u) & ~7u;
    for (uint32_t i = 0; i < FFX_FSR4_TENSOR_COUNT; ++i) {
        schedule.tensorSizes[i][0] = w / tensorDivisors[i];
        schedule.tensorSizes[i][1] = h / tensorDivisors[i];
    }
    tier_extent(outputWidth, outputHeight, &tierWidth, &tierHeight);

    schedule.pre = dispatch_size(div_round_up(w, 16u), div_round_up(h, 16u));
    schedule.model[0] = post_size(schedule.tensorSizes[0], tierWidth, tierHeight, 2u);
    schedule.model[1] = dispatch_size(div_round_up(w, 64u), h / 8u);
    schedule.model[2] = post_size(schedule.tensorSizes[2], tierWidth, tierHeight, 2u);
    schedule.model[3] = post_size(schedule.tensorSizes[3], tierWidth, tierHeight, 4u);
    schedule.model[4] = dispatch_size(div_round_up(w, 256u), h / 4u);
    schedule.model[5] = post_size(schedule.tensorSizes[4], tierWidth, tierHeight, 4u);
    schedule.model[6] = schedule.model[4];
    schedule.model[7] = post_size(schedule.tensorSizes[5], tierWidth, tierHeight, 2u);
    schedule.post = dispatch_size(div_round_up(w, 32u), div_round_up(h, 32u));

    *outSchedule = schedule;
    return true;
}

size_t ffxFsr4GetDot4ScratchSize(uint32_t maxOutputWidth,
                                uint32_t maxOutputHeight)
{
    if (!maxOutputWidth || !maxOutputHeight ||
        maxOutputWidth > FFX_FSR4_DOT4_MAX_OUTPUT_WIDTH ||
        maxOutputHeight > FFX_FSR4_DOT4_MAX_OUTPUT_HEIGHT)
        return 0u;
    if (maxOutputWidth <= 1920u && maxOutputHeight <= 1080u)
        return 25027392u;
    if (maxOutputWidth <= 3840u && maxOutputHeight <= 2160u)
        return 99820992u;
    return 398707392u;
}

const char *ffxFsr4ModelPresetName(FfxFsr4ModelPreset preset)
{
    switch (preset) {
    case FFX_FSR4_MODEL_PRESET_NATIVE_AA:
    case FFX_FSR4_MODEL_PRESET_QUALITY:
    case FFX_FSR4_MODEL_PRESET_BALANCED:
    case FFX_FSR4_MODEL_PRESET_PERFORMANCE:
    case FFX_FSR4_MODEL_PRESET_DRS:
        return "main";
    case FFX_FSR4_MODEL_PRESET_ULTRA_PERFORMANCE:
        return "ultraperf";
    default:
        return "unknown";
    }
}
