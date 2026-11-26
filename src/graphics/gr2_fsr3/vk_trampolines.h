// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "graphics/renderer_vulkan/vk_common.h"

namespace Gr2Fsr3 {

/// Sets the dispatcher of the Vulkan commands that the FidelityFX libraries and the FSR 4
/// provider call: GR2port's device's. Call before the first FidelityFX call; it must outlive them.
void SetDispatcher(const vk::detail::DispatchLoaderDynamic& dispatcher);

/// Sets the pipeline cache of the pipelines the FidelityFX libraries and the FSR 4 provider make
/// without one: GR2port's, which is saved with its own pipelines.
void SetPipelineCache(VkPipelineCache cache);

/// The vkGetDeviceProcAddr for the FidelityFX SDK's backend, whose pipelines then take that cache.
PFN_vkGetDeviceProcAddr DeviceProcAddr();

} // namespace Gr2Fsr3
