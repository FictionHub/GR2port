// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace Core::Loader {
class SymbolsResolver;
}

namespace Libraries::GnmDriver {

/// The compute pipes a game maps queues on, and the queues of each. The system keeps an eighth
/// pipe.
constexpr u32 NumComputePipes = 7;
constexpr u32 NumQueuesPerPipe = 8;

VAddr PS4_SYSV_ABI sceGnmGetTheTessellationFactorRingBufferBaseAddress();
int PS4_SYSV_ABI sceGnmMapComputeQueue(u32 pipe_id, u32 queue_id, VAddr ring_base_addr,
                                       u32 ring_size_dw, u32* read_ptr_addr);
s32 PS4_SYSV_ABI sceGnmSubmitAndFlipCommandBuffers(u32 count, u32* dcb_gpu_addrs[],
                                                   u32* dcb_sizes_in_bytes, u32* ccb_gpu_addrs[],
                                                   u32* ccb_sizes_in_bytes, u32 vo_handle,
                                                   u32 buf_idx, u32 flip_mode, s64 flip_arg);
s32 PS4_SYSV_ABI sceGnmSubmitAndFlipCommandBuffersForWorkload(
    u32 workload, u32 count, u32* dcb_gpu_addrs[], u32* dcb_sizes_in_bytes, u32* ccb_gpu_addrs[],
    u32* ccb_sizes_in_bytes, u32 vo_handle, u32 buf_idx, u32 flip_mode, s64 flip_arg);

void RegisterLib(Core::Loader::SymbolsResolver* sym);
} // namespace Libraries::GnmDriver
