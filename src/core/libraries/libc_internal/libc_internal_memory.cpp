// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/logging/log.h"
#include "core/libraries/error_codes.h"
#include "core/libraries/libs.h"
#include "libc_internal_memory.h"

namespace Libraries::LibcInternal {

void* PS4_SYSV_ABI internal_memset(void* s, int c, size_t n) {
    return std::memset(s, c, n);
}

void* PS4_SYSV_ABI internal_memcpy(void* dest, const void* src, size_t n) {
    return std::memcpy(dest, src, n);
}

s32 PS4_SYSV_ABI internal_memcpy_s(void* dest, size_t destsz, const void* src, size_t count) {
#ifdef _WIN64
    return memcpy_s(dest, destsz, src, count);
#else
    std::memcpy(dest, src, count);
    return 0; // ALL OK
#endif
}

s32 PS4_SYSV_ABI internal_memcmp(const void* s1, const void* s2, size_t n) {
    return std::memcmp(s1, s2, n);
}

void* PS4_SYSV_ABI internal_memmove(void* dest, const void* src, size_t n) {
    return std::memmove(dest, src, n);
}

// The memcmp of the game's own libc module returns the difference of the first two bytes that
// differ. A host's memcmp is only bound to return its sign.
s32 PS4_SYSV_ABI libc_memcmp(const void* s1, const void* s2, size_t n) {
    const u8* a = static_cast<const u8*>(s1);
    const u8* b = static_cast<const u8*>(s2);
    // Equal words are passed over whole. The first that differs is left to the bytes.
    for (; n >= sizeof(u64); a += sizeof(u64), b += sizeof(u64), n -= sizeof(u64)) {
        u64 x;
        u64 y;
        std::memcpy(&x, a, sizeof(x));
        std::memcpy(&y, b, sizeof(y));
        if (x != y) {
            break;
        }
    }
    for (; n != 0; ++a, ++b, --n) {
        if (*a != *b) {
            return static_cast<s32>(*a) - static_cast<s32>(*b);
        }
    }
    return 0;
}

static u64 g_mspace_atomic_id_mask = 0;
static u64 g_mstate_table[64] = {0};

struct HeapInfoInfo {
    u64 size = sizeof(HeapInfoInfo);
    u32 flag;
    u32 getSegmentInfo;
    u64* mspace_atomic_id_mask;
    u64* mstate_table;
};

void PS4_SYSV_ABI sceLibcHeapGetTraceInfo(HeapInfoInfo* info) {
    info->mspace_atomic_id_mask = &g_mspace_atomic_id_mask;
    info->mstate_table = g_mstate_table;
    info->getSegmentInfo = 0;
}

void RegisterlibSceLibcInternalMemory(Core::Loader::SymbolsResolver* sym) {

    LIB_FUNCTION("NFLs+dRJGNg", "libSceLibcInternal", 1, "libSceLibcInternal", internal_memcpy_s);
    LIB_FUNCTION("Q3VBxCXhUHs", "libSceLibcInternal", 1, "libSceLibcInternal", internal_memcpy);
    LIB_FUNCTION("+P6FRGH4LfA", "libSceLibcInternal", 1, "libSceLibcInternal", internal_memmove);
    LIB_FUNCTION("8zTFvBIAIN8", "libSceLibcInternal", 1, "libSceLibcInternal", internal_memset);
    LIB_FUNCTION("DfivPArhucg", "libSceLibcInternal", 1, "libSceLibcInternal", internal_memcmp);

    // The game imports these four from its own libc module, which writes them with string
    // instructions: slow on the host for short operations. The imports take the host's instead,
    // with the same results. The module's memcpy copies as its memmove does, backwards where the
    // destination overlaps the source from above, so both are the host's memmove.
    LIB_FUNCTION("Q3VBxCXhUHs", "libc", 1, "libc", internal_memmove);
    LIB_FUNCTION("+P6FRGH4LfA", "libc", 1, "libc", internal_memmove);
    LIB_FUNCTION("8zTFvBIAIN8", "libc", 1, "libc", internal_memset);
    LIB_FUNCTION("DfivPArhucg", "libc", 1, "libc", libc_memcmp);

    LIB_FUNCTION("NWtTN10cJzE", "libSceLibcInternalExt", 1, "libSceLibcInternal",
                 sceLibcHeapGetTraceInfo);
}

} // namespace Libraries::LibcInternal
