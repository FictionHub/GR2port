// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#include <fmt/ranges.h>

#include "common/logging/log.h"
#include "core/libraries/avplayer/avplayer.h"
#include "graphics/gr2port/capture.h"
#include "graphics/gr2port/capture_math.h"
#include "graphics/gr2port/hooks.h"
#include "graphics/gr2port/movies.h"

namespace Gr2Port::Movies {

namespace {

using Capture::Load;

// FUN_01519400(player): returns 1 when it copied a new frame. The player holds its AvPlayer at
// +0x8, the frame's record at +0x90 when done, and the record it holds before the first frame at
// +0xa0; a record holds the destination's pitch at +0x1a (u16) and its memory at +0x28, whose first
// word is the surface.
constexpr Hooks::Entry Update{0x1519400, 6, Hooks::FramePrologue};
constexpr u64 PlayerAvPlayer = 0x8;
constexpr u64 PlayerRecord = 0x90;
constexpr u64 PlayerPlaceholder = 0xa0;
constexpr u64 RecordPitch = 0x1a;
constexpr u64 RecordTexture = 0x28;
// FUN_01519d40(player) returns a texture object that keeps the player's frame: its memory object
// at +0x28 begins with the surface, which a DMA fills from the surface of the player's record.
constexpr Hooks::Entry Keep{0x1519d40, 6, Hooks::FramePrologue};
constexpr u64 KeptMemory = 0x28;
// The job's constants, as FUN_01519400 writes them: the destination's pitch, the width and height
// it copies, the source's pitch and height, and the crop's left and top.
constexpr u32 Width = 1;
constexpr u32 Height = 2;
constexpr u32 SourcePitch = 3;
constexpr u32 SourceHeight = 4;
constexpr u32 CropLeft = 5;
constexpr u32 CropTop = 6;

constexpr u32 KeptSurfaces = 8;
// The bytes of a surface's copy: AvPlayer's NV12 frame of up to 1920x1088, luma and chroma.
constexpr u64 FrameBytes = 1920 * 1088 * 3 / 2;
// A frame buffer goes back once its slot is unwritten this long, and is deleted GraceSeconds later.
constexpr u32 IdleSeconds = 30;
constexpr u32 GraceSeconds = 5;
constexpr u32 LoggedJobs = 4;
constexpr u32 LoggedKeeps = 8;

// What the game's GPU work last wrote into one surface, taken on the game's thread in the game's
// order: the job's constants and a copy of the frame in AvPlayer's layout. A kept surface also
// names the surface its DMA copied. Read from any thread: the sequence is odd while the words
// change, and a new frame goes into the buffer readers do not read. The generation counts the
// writes of every slot.
struct Slot {
    std::atomic<u32> sequence{0};
    std::atomic<u32> generation{0};
    std::atomic<VAddr> surface{0};
    std::atomic<VAddr> source{0};
    std::atomic<bool> copied{false};
    std::atomic<u32> published{0};
    std::array<std::atomic<u32>, 7> constants{};
    std::array<std::unique_ptr<u8[]>, 2> pixels;
    /// Seconds since the start at the last copy into the slot.
    std::atomic<u32> written{0};
};

// A frame buffer the collector took, deleted once no reader can still hold it.
struct Freed {
    std::unique_ptr<u8[]> buffer;
    u32 at;
};

VAddr g_base = 0;
// The copies, the publishes and the collector take this; readers use the published buffer only.
std::mutex g_buffers;
std::vector<Freed> g_freed;
const auto g_start = std::chrono::steady_clock::now();
std::array<Slot, Surfaces> g_slots;
std::array<Slot, KeptSurfaces> g_kept;
u32 g_next = 0;
u32 g_next_kept = 0;
u32 g_logged = 0;
u32 g_logged_keeps = 0;
u32 g_logged_copies = 0;
u32 g_generation = 0;

// The slot of surface, or the next one round the slots.
template <typename Slots>
Slot& SlotOf(Slots& slots, u32& next, VAddr surface) {
    const auto found = std::ranges::find_if(slots, [surface](const Slot& slot) {
        return slot.surface.load(std::memory_order_relaxed) == surface;
    });
    if (found != slots.end()) {
        return *found;
    }
    Slot& slot = slots[next];
    next = (next + 1) % slots.size();
    return slot;
}

u32 Seconds() {
    return static_cast<u32>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - g_start)
            .count());
}

// Under g_buffers: gives the collector the back buffer of a slot unwritten for IdleSeconds, and
// both buffers of a slot no surface owns.
template <typename Slots>
void CollectSlots(Slots& slots, u32 now) {
    for (Slot& slot : slots) {
        const bool owned = slot.surface.load(std::memory_order_relaxed) != 0;
        const u32 published = slot.published.load(std::memory_order_relaxed);
        if (owned && now - slot.written.load(std::memory_order_relaxed) < IdleSeconds) {
            continue;
        }
        for (u32 i = 0; i < slot.pixels.size(); i++) {
            if ((owned && i == published) || !slot.pixels[i]) {
                continue;
            }
            g_freed.push_back({std::move(slot.pixels[i]), now});
        }
    }
}

// Under g_buffers, held until the copy is published: copies bytes from data into the buffer of
// slot readers do not read; false when they do not fit.
bool CopyFrame(Slot& slot, const void* data, u64 bytes) {
    if (data == nullptr || bytes == 0 || bytes > FrameBytes) {
        return false;
    }
    slot.written.store(Seconds(), std::memory_order_relaxed);
    auto& buffer = slot.pixels[slot.published.load(std::memory_order_relaxed) ^ 1];
    if (!buffer) {
        buffer = std::make_unique<u8[]>(FrameBytes);
    }
    const auto start = std::chrono::steady_clock::now();
    std::memcpy(buffer.get(), data, bytes);
    if (g_logged_copies < LoggedKeeps) {
        g_logged_copies++;
        LOG_INFO(Render_Vulkan, "GR2port movies: frame copied, {} bytes in {} us", bytes,
                 std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::steady_clock::now() - start)
                     .count());
    }
    return true;
}

// Under g_buffers.
void Publish(Slot& slot, VAddr surface, VAddr source, bool copied,
             const std::array<u32, 7>& values) {
    const u32 sequence = slot.sequence.load(std::memory_order_relaxed);
    slot.sequence.store(sequence + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    slot.generation.store(++g_generation, std::memory_order_relaxed);
    slot.surface.store(surface, std::memory_order_relaxed);
    slot.source.store(source, std::memory_order_relaxed);
    slot.copied.store(copied, std::memory_order_relaxed);
    if (copied) {
        slot.published.store(slot.published.load(std::memory_order_relaxed) ^ 1,
                             std::memory_order_relaxed);
        for (u32 i = 0; i < values.size(); i++) {
            slot.constants[i].store(values[i], std::memory_order_relaxed);
        }
    }
    slot.sequence.store(sequence + 2, std::memory_order_release);
}

// The published frame of slot, read under its sequence.
Frame Read(const Slot& slot, VAddr surface, VAddr* source = nullptr) {
    Frame frame{};
    u32 before = 0;
    do {
        before = slot.sequence.load(std::memory_order_acquire);
        frame.found = slot.copied.load(std::memory_order_relaxed);
        frame.generation = slot.generation.load(std::memory_order_relaxed);
        frame.source = reinterpret_cast<VAddr>(
            slot.pixels[slot.published.load(std::memory_order_relaxed)].get());
        for (u32 i = 0; i < frame.constants.size(); i++) {
            frame.constants[i] = slot.constants[i].load(std::memory_order_relaxed);
        }
        if (source != nullptr) {
            *source = slot.source.load(std::memory_order_relaxed);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
    } while ((before & 1) != 0 || slot.sequence.load(std::memory_order_relaxed) != before);
    return slot.surface.load(std::memory_order_relaxed) == surface ? frame : Frame{};
}

// A surface is a movie's or a kept frame's, whichever the game wrote last: AvPlayer and the keeps
// share their memory, so a new owner of an address drops the slot of the old one.
template <typename Slots>
void Drop(Slots& slots, VAddr surface) {
    for (auto& slot : slots) {
        if (slot.surface.load(std::memory_order_relaxed) != surface) {
            continue;
        }
        const u32 sequence = slot.sequence.load(std::memory_order_relaxed);
        slot.sequence.store(sequence + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        slot.surface.store(0, std::memory_order_relaxed);
        slot.sequence.store(sequence + 2, std::memory_order_release);
    }
}

// The job of the frame FUN_01519400 just took: its destination is the surface of the player's
// record, its source the frame the game's last sceAvPlayerGetVideoDataEx gave it (FUN_01519400 is
// the game's only caller), and its constants come from that frame's pitch, height and crop.
void PS4_SYSV_ABI OnUpdate(u32, const Hooks::Arguments* arguments, u64 result) {
    if ((result & 0xff) == 0) {
        return;
    }
    const VAddr player = arguments->rdi;
    const VAddr record = Load<VAddr>(player + PlayerRecord);
    const VAddr texture = record != 0 ? Load<VAddr>(record + RecordTexture) : 0;
    const VAddr surface = texture != 0 ? Load<VAddr>(texture) : 0;
    Libraries::AvPlayer::AvPlayerFrameInfoEx info{};
    if (surface == 0 ||
        !Libraries::AvPlayer::LastVideoData(
            Load<Libraries::AvPlayer::AvPlayerHandle>(player + PlayerAvPlayer), info)) {
        return;
    }
    const auto& video = info.details.video;
    const std::array<u32, 7> values{
        Load<u16>(record + RecordPitch),
        video.pitch - video.crop_left_offset - video.crop_right_offset,
        video.height - video.crop_top_offset - video.crop_bottom_offset,
        video.pitch,
        video.height,
        video.crop_left_offset,
        video.crop_top_offset,
    };
    const VAddr source = reinterpret_cast<VAddr>(info.p_data);
    if (g_logged < LoggedJobs) {
        g_logged++;
        LOG_INFO(Render_Vulkan, "GR2port movies: frame into {:#x} from {:#x}, constants {}",
                 surface, source, fmt::join(values, " "));
    }
    if (source == 0) {
        return;
    }
    // AvPlayer reuses its frame two decodes later, so the frame is copied now, as the job reads it.
    Drop(g_kept, surface);
    Slot& slot = SlotOf(g_slots, g_next, surface);
    // The collector takes the buffers of a slot no surface owns, so it waits for the publish.
    std::scoped_lock lock{g_buffers};
    const bool copied = CopyFrame(slot, reinterpret_cast<const void*>(source),
                                  u64{values[SourcePitch]} * values[SourceHeight] * 3 / 2);
    Publish(slot, surface, 0, copied, values);
}

// The keep FUN_01519d40 just made: its DMA copies from the surface of the player's record, except
// before the first frame, when the record is the placeholder and the keep is a 1x1 texture of its
// colour.
void PS4_SYSV_ABI OnKeep(u32, const Hooks::Arguments* arguments, u64 result) {
    const VAddr memory = result != 0 ? Load<VAddr>(result + KeptMemory) : 0;
    const VAddr surface = memory != 0 ? Load<VAddr>(memory) : 0;
    if (surface == 0) {
        return;
    }
    const VAddr player = arguments->rdi;
    const VAddr record = Load<VAddr>(player + PlayerRecord);
    const VAddr record_memory = record != 0 && record != Load<VAddr>(player + PlayerPlaceholder)
                                    ? Load<VAddr>(record + RecordTexture)
                                    : 0;
    const VAddr source = record_memory != 0 ? Load<VAddr>(record_memory) : 0;
    if (g_logged_keeps < LoggedKeeps) {
        g_logged_keeps++;
        LOG_INFO(Render_Vulkan, "GR2port movies: frame of {:#x} kept in {:#x}", source, surface);
    }
    if (source == 0) {
        return;
    }
    // The DMA copies what the source holds now: the copy of the source's last job.
    Drop(g_slots, surface);
    Slot& slot = SlotOf(g_kept, g_next_kept, surface);
    std::scoped_lock lock{g_buffers};
    const auto job = std::ranges::find_if(g_slots, [source](const Slot& job) {
        return job.surface.load(std::memory_order_relaxed) == source &&
               job.copied.load(std::memory_order_relaxed);
    });
    std::array<u32, 7> values{};
    bool copied = false;
    if (job != g_slots.end()) {
        for (u32 i = 0; i < values.size(); i++) {
            values[i] = job->constants[i].load(std::memory_order_relaxed);
        }
        copied = CopyFrame(slot, job->pixels[job->published.load(std::memory_order_relaxed)].get(),
                           u64{values[SourcePitch]} * values[SourceHeight] * 3 / 2);
    }
    Publish(slot, surface, source, copied, values);
}

} // namespace

bool Install(VAddr module_base, Xbyak::CodeGenerator& c, size_t capacity) {
    g_base = module_base;
    return Hooks::Hooked(Hooks::ObserveReturn(module_base, Update, c, capacity, &OnUpdate, 0)) &&
           Hooks::Hooked(Hooks::ObserveReturn(module_base, Keep, c, capacity, &OnKeep, 0));
}

Job Peek(u32 slot) {
    const Slot& s = g_slots[slot];
    Job job{};
    u32 before = 0;
    do {
        before = s.sequence.load(std::memory_order_acquire);
        job.surface = s.surface.load(std::memory_order_relaxed);
        job.generation = s.generation.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
    } while ((before & 1) != 0 || s.sequence.load(std::memory_order_relaxed) != before);
    return job;
}

Kept FindKept(VAddr surface) {
    for (const Slot& slot : g_kept) {
        if (slot.surface.load(std::memory_order_relaxed) != surface) {
            continue;
        }
        Kept kept{};
        kept.frame = Read(slot, surface, &kept.source);
        kept.generation = kept.frame.generation;
        return kept.source != 0 ? kept : Kept{};
    }
    return {};
}

void Collect() {
    std::scoped_lock lock{g_buffers};
    // Read under the lock, so no copy is later than it.
    const u32 now = Seconds();
    CollectSlots(g_slots, now);
    CollectSlots(g_kept, now);
    std::erase_if(g_freed, [now](const Freed& f) { return now - f.at >= GraceSeconds; });
}

Frame Find(VAddr surface) {
    for (u32 i = 0; i < Surfaces; i++) {
        const Slot& slot = g_slots[i];
        if (slot.surface.load(std::memory_order_relaxed) != surface) {
            continue;
        }
        Frame frame = Read(slot, surface);
        frame.slot = i;
        return frame;
    }
    return {};
}

void Write(const Frame& frame, u8* out, u32 pitch, u32 rows) {
    const auto& c = frame.constants;
    const u32 width = std::min(c[Width], pitch);
    const u32 height = std::min(c[Height], rows);
    const u32 chroma_rows = std::min(c[Height] / 2, rows - height);
    const u8* const luma = reinterpret_cast<const u8*>(frame.source);
    const u8* const chroma = luma + u64{c[SourcePitch]} * c[SourceHeight];
    for (u32 y = 0; y < height; y++) {
        std::memcpy(out + u64{y} * pitch, luma + u64{y + c[CropTop]} * c[SourcePitch] + c[CropLeft],
                    width);
    }
    for (u32 y = 0; y < chroma_rows; y++) {
        std::memcpy(out + u64{height + y} * pitch,
                    chroma + u64{y + c[CropTop] / 2} * c[SourcePitch] + c[CropLeft], width);
    }
}

} // namespace Gr2Port::Movies
