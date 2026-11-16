// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "graphics/renderer_vulkan/vk_common.h"

namespace Gr2Fsr3 {

/// A colour image with one mip and one layer.
struct Fsr4Image {
    vk::Image image{};
    vk::ImageView view{};
};

/// The images of an FSR 4 context, registered with the provider once: the inputs at the render
/// size, the output at the output size.
struct Fsr4Images {
    Fsr4Image color;          ///< R16G16B16A16_SFLOAT
    Fsr4Image motion_vectors; ///< R16G16_SFLOAT
    Fsr4Image depth;          ///< R32_SFLOAT
    Fsr4Image output;         ///< R16G16B16A16_SFLOAT
};

} // namespace Gr2Fsr3
