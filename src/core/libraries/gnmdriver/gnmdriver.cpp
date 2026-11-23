// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/libraries/gnmdriver/gnmdriver.h"
#include "core/libraries/libs.h"
#include "core/libraries/videoout/video_out.h"
#include "core/libraries/videoout/videoout_error.h"
#include "core/memory.h"
#include "graphics/gr2port/presenter.h"

extern Frontend::WindowSDL* g_window;
std::unique_ptr<Gr2Port::Presenter> presenter;

namespace Libraries::GnmDriver {

// GR2port: the number of compute queues the game mapped. Nothing submits to them, so a queue is
// only its number.
static u32 mapped_queues = 0;

VAddr PS4_SYSV_ABI sceGnmGetTheTessellationFactorRingBufferBaseAddress() {
    auto& address_space = Core::Memory::Instance()->GetAddressSpace();
    return address_space.SystemReservedVirtualBase() + address_space.SystemReservedVirtualSize() -
           0x10000000;
}

int PS4_SYSV_ABI sceGnmMapComputeQueue(u32 pipe_id, u32 queue_id, VAddr ring_base_addr,
                                       u32 ring_size_dw, u32* read_ptr_addr) {
    // The system numbers the queues from 1.
    return ++mapped_queues;
}

s32 PS4_SYSV_ABI sceGnmSubmitAndFlipCommandBuffers(u32 count, u32* dcb_gpu_addrs[],
                                                   u32* dcb_sizes_in_bytes, u32* ccb_gpu_addrs[],
                                                   u32* ccb_sizes_in_bytes, u32 vo_handle,
                                                   u32 buf_idx, u32 flip_mode, s64 flip_arg) {
    return sceGnmSubmitAndFlipCommandBuffersForWorkload(
        count, count, dcb_gpu_addrs, dcb_sizes_in_bytes, ccb_gpu_addrs, ccb_sizes_in_bytes,
        vo_handle, buf_idx, flip_mode, flip_arg);
}

// GR2port: no GPU runs the command buffers, so a flip goes to the video out when it is submitted.
// Gr2Port::GpuWrites makes the writes of its frame as the game adds them.
s32 PS4_SYSV_ABI sceGnmSubmitAndFlipCommandBuffersForWorkload(
    u32 workload, u32 count, u32* dcb_gpu_addrs[], u32* dcb_sizes_in_bytes, u32* ccb_gpu_addrs[],
    u32* ccb_sizes_in_bytes, u32 vo_handle, u32 buf_idx, u32 flip_mode, s64 flip_arg) {
    const s32 result =
        VideoOut::sceVideoOutSubmitEopFlip(vo_handle, buf_idx, flip_mode, flip_arg, nullptr);
    return result == ORBIS_VIDEO_OUT_ERROR_FLIP_QUEUE_FULL ? 0x80d11081 : result;
}

void RegisterLib(Core::Loader::SymbolsResolver* sym) {
    // GR2port: there is no command processor; Gr2Port::GpuWrites makes the GPU writes.
    presenter = std::make_unique<Gr2Port::Presenter>(*g_window);

    LIB_FUNCTION("ln33zjBrfjk", "libSceGnmDriver", 1, "libSceGnmDriver",
                 sceGnmGetTheTessellationFactorRingBufferBaseAddress);
    LIB_FUNCTION("29oKvKXzEZo", "libSceGnmDriver", 1, "libSceGnmDriver", sceGnmMapComputeQueue);
    LIB_FUNCTION("xbxNatawohc", "libSceGnmDriver", 1, "libSceGnmDriver",
                 sceGnmSubmitAndFlipCommandBuffers);
    LIB_FUNCTION("Ga6r7H6Y0RI", "libSceGnmDriver", 1, "libSceGnmDriver",
                 sceGnmSubmitAndFlipCommandBuffersForWorkload);
};

} // namespace Libraries::GnmDriver
