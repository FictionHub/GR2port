// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <utility>

#include "common/logging/log.h"
#include "graphics/gr2port/frame_graph_draws.h"
#include "graphics/gr2port/hooks.h"

namespace Gr2Port::FrameGraphDraws {

namespace {

// SubmitCategory FUN_014214e0, the frame graph's per-item draw loop. Its handlers read the items
// and write GNM commands, which nothing reads.
constexpr Hooks::Entry DrawLoop{0x14214e0, 6, Hooks::FramePrologue};

// What the game calls for each draw and dispatch on a graphics context. FUN_0042dc50 binds a
// draw's shaders and writes their constants, FUN_0042da70 does the same for a dispatch, and the
// others write the draw packets.
constexpr std::array Writers{
    Hooks::Entry{0x42dc50, 6, Hooks::FramePrologue},
    Hooks::Entry{0x42da70, 6, Hooks::FramePrologue},
    Hooks::Entry{0x412c50, 6, Hooks::FramePrologue},
    Hooks::Entry{0x412820, 6, Hooks::FramePrologue},
    // The binders copy a stage's constant buffers, buffers, read-write buffers, textures,
    // read-write textures and samplers for its next draw on a graphics context, then for the next
    // dispatch on a compute queue.
    Hooks::Entry{0x42f410, 6, Hooks::FramePrologue},
    Hooks::Entry{0x42f690, 6, Hooks::FramePrologue},
    Hooks::Entry{0x42f7d0, 6, Hooks::FramePrologue},
    Hooks::Entry{0x42f910, 6, Hooks::FramePrologue},
    Hooks::Entry{0x42fa50, 6, Hooks::FramePrologue},
    Hooks::Entry{0x42fb90, 6, Hooks::FramePrologue},
    Hooks::Entry{0x42d1d0, 6, Hooks::FramePrologue},
    Hooks::Entry{0x42d290, 6, Hooks::FramePrologue},
    Hooks::Entry{0x42d350, 6, Hooks::FramePrologue},
    Hooks::Entry{0x42d410, 6, Hooks::FramePrologue},
    Hooks::Entry{0x42d4d0, 6, Hooks::FramePrologue},
    Hooks::Entry{0x42d590, 6, Hooks::FramePrologue},
    // The writers of what the frame graph's passes still send: the registers of the draw
    // wrappers' state flush FUN_01614150, the targets and viewports of FUN_016137a0,
    // FUN_01616ee0, FUN_0161b3e0 and FUN_0161b760, the index buffer of FUN_01616e10, and the
    // pipeline events. Each writes its packet and nothing else; the callers keep the engine's
    // copy of the state, which UiCapture reads.
    Hooks::Entry{0x411550, 6, Hooks::FramePrologueR14},
    Hooks::Entry{0x411610, 6, Hooks::FramePrologue},
    Hooks::Entry{0x411680, 5, {0x55, 0x48, 0x89, 0xe5, 0x53}},
    Hooks::Entry{0x411710, 6, Hooks::FramePrologueR14},
    Hooks::Entry{0x4118a0, 6, Hooks::FramePrologue},
    Hooks::Entry{0x411920, 6, Hooks::FramePrologueR14},
    Hooks::Entry{0x411980, 6, Hooks::FramePrologueR14},
    Hooks::Entry{0x4119e0, 6, Hooks::FramePrologueR14},
    Hooks::Entry{0x411a40, 5, {0x55, 0x48, 0x89, 0xe5, 0x53}},
    Hooks::Entry{0x411aa0, 5, {0x55, 0x48, 0x89, 0xe5, 0x53}},
    Hooks::Entry{0x411b10, 5, {0x55, 0x48, 0x89, 0xe5, 0x53}},
    Hooks::Entry{0x411b70, 6, Hooks::FramePrologueR14},
    Hooks::Entry{0x411bd0, 6, Hooks::FramePrologue},
    Hooks::Entry{0x411c50, 5, {0x55, 0x48, 0x89, 0xe5, 0x53}},
    Hooks::Entry{0x411d60, 6, Hooks::FramePrologue},
    Hooks::Entry{0x411e50, 6, Hooks::FramePrologueR14},
    Hooks::Entry{0x412520, 6, Hooks::FramePrologueR14},
    Hooks::Entry{0x412590, 6, Hooks::FramePrologueR14},
    Hooks::Entry{0x413b10, 6, Hooks::FramePrologue},
    Hooks::Entry{0x413b90, 6, Hooks::FramePrologueR14},
    Hooks::Entry{0x413bf0, 6, Hooks::FramePrologueR14},
    Hooks::Entry{0x4148e0, 6, Hooks::FramePrologueR14},
    Hooks::Entry{0x415120, 6, Hooks::FramePrologue},
    Hooks::Entry{0x4152c0, 6, Hooks::FramePrologue},
    Hooks::Entry{0x4154b0, 6, Hooks::FramePrologueR14},
    Hooks::Entry{0x415690, 6, Hooks::FramePrologueR14},
    Hooks::Entry{0x415b10, 6, Hooks::FramePrologueR14},
    Hooks::Entry{0x415b70, 6, Hooks::FramePrologueR14},
    Hooks::Entry{0x415bd0, 6, Hooks::FramePrologue},
    Hooks::Entry{0x415d10, 6, Hooks::FramePrologueR14},
    Hooks::Entry{0x415d80, 5, {0x55, 0x48, 0x89, 0xe5, 0x53}},
    Hooks::Entry{0x415df0, 5, {0x55, 0x48, 0x89, 0xe5, 0x53}},
    Hooks::Entry{0x415e60, 6, Hooks::FramePrologue},
    Hooks::Entry{0x415ed0, 6, Hooks::FramePrologue},
    Hooks::Entry{0x415f50, 6, Hooks::FramePrologueR14},
};

// push rbp; mov rbp, rsp.
constexpr std::array<u8, 11> PushFrame{0x55, 0x48, 0x89, 0xe5};

// UiCapture reads the programs, registers and draws of the screen drawing. The functions below
// write none of them, so they return at once: a compute queue's dispatch, FUN_0042cfb0 and
// FUN_00413560, the functions that reserve room for one packet, call the driver and advance, and
// the thunks of two driver functions whose results the game leaves unused.
constexpr std::array Returning{
    Hooks::Entry{0x42cfb0, 4, PushFrame},
    Hooks::Entry{0x413560, 4, PushFrame},
    Hooks::Entry{0x4135d0, 4, PushFrame}, // sceGnmDispatchDirect
    Hooks::Entry{0x429510, 4, PushFrame}, // sceGnmDispatchInitDefaultHardwareState
    Hooks::Entry{0x410e50, 4, PushFrame}, // sceGnmDrawInitDefaultHardwareState350
    Hooks::Entry{0x412d60, 4, PushFrame}, // sceGnmDrawIndirect
    Hooks::Entry{0x416c10, 4, PushFrame}, // sceGnmInsertPushMarker
    Hooks::Entry{0x416d10, 4, PushFrame}, // sceGnmInsertPopMarker
    Hooks::Entry{0x417070, 4, PushFrame}, // sceGnmInsertWaitFlipDone
    Hooks::Entry{0x4141e0, 4, PushFrame}, // sceGnmSetCsShaderWithModifier
    Hooks::Entry{0x413e20, 4, PushFrame}, // sceGnmSetEmbeddedVsShader
    Hooks::Entry{0x414510, 4, PushFrame}, // sceGnmSetGsShader
    Hooks::Entry{0x414570, 4, PushFrame}, // sceGnmUpdateGsShader
    Hooks::Entry{0x413f70, 4, PushFrame}, // sceGnmSetHsShader
    Hooks::Entry{0x413fe0, 4, PushFrame}, // sceGnmUpdateHsShader
    Hooks::Entry{0x414170, 4, PushFrame}, // sceGnmSetLsShader
    Hooks::Entry{0x414430, 4, PushFrame}, // sceGnmSetVgtControl
    Hooks::Entry{0x4144a0, 4, PushFrame}, // sceGnmSetVgtControl
    Hooks::Entry{0x429130, 5, {0xe9, 0xfb, 0xee, 0x3c, 0x01}}, // sceGnmFlushGarlic
    Hooks::Entry{0x4291b0, 5, {0xe9, 0xcb, 0xec, 0x3c, 0x01}}, // sceGnmSubmitDone
    // A compute command buffer's cache invalidation and shader control, and its submit to a
    // compute queue, whose result the game leaves unused. Nothing runs the compute queues.
    Hooks::Entry{0x429b40, 4, PushFrame},
    Hooks::Entry{0x4112d0, 4, PushFrame},
    Hooks::Entry{0x42ab60, 4, PushFrame},
    // A graphics command buffer's cache flushes and waits, among them the frame's first flush and
    // its last wait, and the room the frame leaves at its end for flip packets, which the submit
    // does not write.
    Hooks::Entry{0x414950, 4, PushFrame},
    Hooks::Entry{0x416730, 4, PushFrame},
    Hooks::Entry{0x416b40, 4, PushFrame},
    Hooks::Entry{0x4170d0, 4, PushFrame},
    // The frame graph's depth sort orders the G2_BG_Main keys for the draw loop. Capture reads the
    // keys when the frame graph starts.
    Hooks::Entry{0x1441460, 4, PushFrame},
    // The particle draw passes bind and draw the draw list that ParticleCapture reads when
    // FUN_014a3ba0 returns.
    Hooks::Entry{0x14baaa0, 4, PushFrame},
    // The frame graph's binders of constants and textures copy constants into the command buffer
    // and set a texture's memory type and sampler words for its programs.
    Hooks::Entry{0x16167f0, 4, PushFrame},
    Hooks::Entry{0x1616c10, 4, PushFrame},
    // The particle compute passes dispatch the game's simulation programs and signal the labels
    // the graphics context waits for. ParticleCapture reads the simulation's input.
    Hooks::Entry{0x14a4a10, 4, PushFrame},
    Hooks::Entry{0x14a7350, 4, PushFrame},
};

using WriterFunction = void PS4_SYSV_ABI (*)(u64, u64, u64, u64, u64, u64, u64, double, double,
                                             double, double);
std::array<WriterFunction, Writers.size()> g_writers{};

// On the game's draw thread: whether the frame graph runs.
thread_local bool t_frame_graph = false;

// In place of writer Index, which writes nothing while the frame graph runs. A writer takes at
// most seven integer arguments and four float arguments, which OnWriter passes on.
template <size_t Index>
void PS4_SYSV_ABI OnWriter(u64 a, u64 b, u64 c, u64 d, u64 e, u64 f, u64 g, double x0, double x1,
                           double x2, double x3) {
    if (!t_frame_graph) {
        g_writers[Index](a, b, c, d, e, f, g, x0, x1, x2, x3);
    }
}

template <size_t... Indices>
std::array<const void*, sizeof...(Indices)> WriterTargets(std::index_sequence<Indices...>) {
    return {reinterpret_cast<const void*>(&OnWriter<Indices>)...};
}

} // namespace

void Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity) {
    if (Hooks::SetReturning(module_base, DrawLoop, true) != Hooks::Result::Hooked) {
        LOG_ERROR(Render_Vulkan, "GR2port: the draw loop is not the expected code");
    }
    const auto targets = WriterTargets(std::make_index_sequence<Writers.size()>{});
    for (size_t i = 0; i < Writers.size(); i++) {
        const void* original = nullptr;
        if (Hooks::Replace(module_base, Writers[i], c, capacity, targets[i], &original) !=
            Hooks::Result::Hooked) {
            LOG_ERROR(Render_Vulkan, "GR2port: the function at {:#x} is not the expected code",
                      Writers[i].address);
            continue;
        }
        g_writers[i] = reinterpret_cast<WriterFunction>(const_cast<void*>(original));
    }
    for (const Hooks::Entry& entry : Returning) {
        if (Hooks::Return(module_base, entry) != Hooks::Result::Hooked) {
            LOG_ERROR(Render_Vulkan, "GR2port: the function at {:#x} is not the expected code",
                      entry.address);
        }
    }
}

void SetFrameGraphRunning(bool running) {
    t_frame_graph = running;
}

} // namespace Gr2Port::FrameGraphDraws
