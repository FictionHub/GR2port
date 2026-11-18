// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "graphics/gr2port/settings.h"

// The render thread: draws each scene the capture publishes into the renderer's window.
namespace Gr2Port::Renderer {

/// Starts the render thread, which creates the window and the device.
void Start();

/// The settings the renderer draws with once it took wanted, in a game that started with
/// started: wanted's where a setting changes while the game runs, started's elsewhere. Shadows,
/// fog shadows, motion blur, antialiasing, bloom, outlines, decals, glass refraction, anisotropic
/// filtering, the texture streaming budget and VSync change, and so do the field of view and the
/// screen shake, which FieldOfView and ScreenShake take; ambient occlusion turns off and on at the
/// size it started with.
Settings::Values Live(const Settings::Values& started, const Settings::Values& wanted);

/// Any thread, after the stored settings changed: the render thread takes them before its next
/// frame.
void SettingsChanged();

} // namespace Gr2Port::Renderer
