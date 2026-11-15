// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>

#include "common/logging/log.h"
#include "common/singleton.h"
#include "input/controller.h"

#include "graphics/gr2port/hooks.h"
#include "graphics/gr2port/stick_tilt.h"

namespace Gr2Port::StickTilt {

namespace {

// void FUN_01397200(input, bool reset): a menu input's tilt. The reference quaternion R at
// input + 0x120, which a reset sets to the inverse of the pad's orientation Q, gives the tilt
// R * Q. Its X and -Z components, each held within the sine of half the angle at input + 0x184
// (in degrees), are what the menu turns by.
constexpr Hooks::Entry Tilt{0x1397200, 6, Hooks::FramePrologueR14};
constexpr u64 Reference = 0x120;
constexpr u64 MaxDegrees = 0x184;

// FUN_01396ec0 copies the right stick into the input: x right, y up, each -1 to 1.
constexpr u64 RightStick = 0x100;

struct Quaternion {
    float x;
    float y;
    float z;
    float w;
};

using TiltFunction = u64 PS4_SYSV_ABI (*)(u64 input, u64 reset);

TiltFunction g_tilt = nullptr;

Quaternion Multiply(const Quaternion& a, const Quaternion& b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

template <typename T>
T Load(u64 address) {
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}

template <typename T>
void Store(u64 address, const T& value) {
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
}

// While the original runs, the reference also turns by the stick, so the tilt is the stick's
// turn after the pad's own. A reset keeps the game's reference.
u64 PS4_SYSV_ABI OnTilt(u64 input, u64 reset) {
    const auto stick = Load<std::array<float, 2>>(input + RightStick);
    if (static_cast<u8>(reset) != 0 || (stick[0] == 0.0f && stick[1] == 0.0f) ||
        (*Common::Singleton<Input::GameControllers>::Instance())[0]->HasGyro()) {
        return g_tilt(input, reset);
    }
    // Up raises the pad's top and right lowers its right side, as a gyro reports them.
    const float limit =
        std::sin(Load<float>(input + MaxDegrees) * (std::numbers::pi_v<float> / 360.0f));
    const float x = stick[1] * limit;
    const float z = -stick[0] * limit;
    const Quaternion turn{x, 0.0f, z, std::sqrt(std::max(0.0f, 1.0f - x * x - z * z))};
    const auto reference = Load<Quaternion>(input + Reference);
    Store(input + Reference, Multiply(turn, reference));
    const u64 result = g_tilt(input, reset);
    Store(input + Reference, reference);
    return result;
}

} // namespace

bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity) {
    const void* original = nullptr;
    if (Hooks::Replace(module_base, Tilt, c, capacity, reinterpret_cast<const void*>(&OnTilt),
                       &original) != Hooks::Result::Hooked) {
        LOG_ERROR(Render_Vulkan, "GR2port: the menu tilt is not the expected code");
        return false;
    }
    g_tilt = reinterpret_cast<TiltFunction>(const_cast<void*>(original));
    return true;
}

} // namespace Gr2Port::StickTilt
