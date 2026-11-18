// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>

#include "core/libraries/error_codes.h"
#include "core/libraries/gnmdriver/gnmdriver.h"
#include "core/libraries/libs.h"
#include "core/libraries/videoout/video_out.h"
#include "core/libraries/videoout/videoout_error.h"
#include "core/memory.h"
#include "graphics/gr2port/presenter.h"

extern Frontend::WindowSDL* g_window;
std::unique_ptr<Gr2Port::Presenter> presenter;

namespace Libraries::GnmDriver {

// GR2port: the compute rings the game mapped. No GPU runs them, so the read pointer, which the game
// reads for a ring's free space, follows each new end the doorbell gives.
struct ComputeRing {
    u32* read_pointer;
    u32 size_dwords;
};
static std::array<ComputeRing, NumComputePipes * NumQueuesPerPipe> compute_rings{};
static u32 mapped_rings = 0;

// GR2port: Gr2Port::UiCapture reads the low words of the programs' addresses from what the shader
// functions write; every program the game binds lies below 1 TiB, so the high words stay 0. A
// shader function writes the low word and a NOP over the rest of the space the game reserved.
static s32 WriteProgram(u32* cmdbuf, u32 size, u32 reg, u32 address) {
    cmdbuf[0] = 0xc0017600u; // SET_SH_REG of one register.
    cmdbuf[1] = reg;
    cmdbuf[2] = address;
    cmdbuf[3] = 0xc0001000u | (size - 5) << 16;
    return ORBIS_OK;
}

void PS4_SYSV_ABI sceGnmDingDong(u32 gnm_vqid, u32 next_offs_dw) {
    const ComputeRing& ring = compute_rings[gnm_vqid - 1];
    // The system maps only rings whose size is a power of two.
    *ring.read_pointer = next_offs_dw & (ring.size_dwords - 1);
}

// GR2port: the draw packets cover the space the game reserved, with the words Gr2Port::UiCapture
// reads.
s32 PS4_SYSV_ABI sceGnmDrawIndexAuto(u32* cmdbuf, u32 size, u32 index_count, u32 flags) {
    cmdbuf[0] = 0xc0002d00u | (size - 2) << 16; // DRAW_INDEX_AUTO
    cmdbuf[1] = index_count;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceGnmDrawIndexOffset(u32* cmdbuf, u32 size, u32 index_offset, u32 index_count,
                                       u32 flags) {
    cmdbuf[0] = 0xc0003500u | (size - 2) << 16; // DRAW_INDEX_OFFSET_2
    cmdbuf[2] = index_offset;
    cmdbuf[3] = index_count;
    return ORBIS_OK;
}

VAddr PS4_SYSV_ABI sceGnmGetTheTessellationFactorRingBufferBaseAddress() {
    auto& address_space = Core::Memory::Instance()->GetAddressSpace();
    return address_space.SystemReservedVirtualBase() + address_space.SystemReservedVirtualSize() -
           0x10000000;
}

int PS4_SYSV_ABI sceGnmMapComputeQueue(u32 pipe_id, u32 queue_id, VAddr ring_base_addr,
                                       u32 ring_size_dw, u32* read_ptr_addr) {
    compute_rings[mapped_rings] = {read_ptr_addr, ring_size_dw};
    // The doorbell numbers the queues from 1.
    return ++mapped_rings;
}

s32 PS4_SYSV_ABI sceGnmSetEsShader(u32* cmdbuf, u32 size, const u32* es_regs, u32 shader_modifier) {
    return WriteProgram(cmdbuf, size, 0xc8u, es_regs[0]); // SPI_SHADER_PGM_LO_ES
}

s32 PS4_SYSV_ABI sceGnmSetPsShader350(u32* cmdbuf, u32 size, const u32* ps_regs) {
    return WriteProgram(cmdbuf, size, 0x8u, ps_regs ? ps_regs[0] : 0u); // SPI_SHADER_PGM_LO_PS
}

s32 PS4_SYSV_ABI sceGnmSetVsShader(u32* cmdbuf, u32 size, const u32* vs_regs, u32 shader_modifier) {
    return WriteProgram(cmdbuf, size, 0x48u, vs_regs[0]); // SPI_SHADER_PGM_LO_VS
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

    LIB_FUNCTION("bX5IbRvECXk", "libSceGnmDriver", 1, "libSceGnmDriver", sceGnmDingDong);
    LIB_FUNCTION("GGsn7jMTxw4", "libSceGnmDriver", 1, "libSceGnmDriver", sceGnmDrawIndexAuto);
    LIB_FUNCTION("oYM+YzfCm2Y", "libSceGnmDriver", 1, "libSceGnmDriver", sceGnmDrawIndexOffset);
    LIB_FUNCTION("ln33zjBrfjk", "libSceGnmDriver", 1, "libSceGnmDriver",
                 sceGnmGetTheTessellationFactorRingBufferBaseAddress);
    LIB_FUNCTION("29oKvKXzEZo", "libSceGnmDriver", 1, "libSceGnmDriver", sceGnmMapComputeQueue);
    LIB_FUNCTION("FUHG8sQ3R58", "libSceGnmDriver", 1, "libSceGnmDriver", sceGnmSetEsShader);
    LIB_FUNCTION("5uFKckiJYRM", "libSceGnmDriver", 1, "libSceGnmDriver", sceGnmSetPsShader350);
    LIB_FUNCTION("gAhCn6UiU4Y", "libSceGnmDriver", 1, "libSceGnmDriver", sceGnmSetVsShader);
    LIB_FUNCTION("xbxNatawohc", "libSceGnmDriver", 1, "libSceGnmDriver",
                 sceGnmSubmitAndFlipCommandBuffers);
    LIB_FUNCTION("Ga6r7H6Y0RI", "libSceGnmDriver", 1, "libSceGnmDriver",
                 sceGnmSubmitAndFlipCommandBuffersForWorkload);
    // GR2port: the update functions write what the set functions write.
    LIB_FUNCTION("mLVL7N7BVBg", "libSceGnmDriver", 1, "libSceGnmDriver", sceGnmSetPsShader350);
    LIB_FUNCTION("V31V01UiScY", "libSceGnmDriver", 1, "libSceGnmDriver", sceGnmSetVsShader);
};

} // namespace Libraries::GnmDriver
