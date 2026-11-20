// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

#include "common/logging/log.h"
#include "graphics/gr2port/catcher_crash.h"
#include "graphics/gr2port/catcher_test.h"
#include "graphics/gr2port/memory.h"

namespace Gr2Port::Catcher::Test {

namespace {

enum class Fault : u32 {
    None,
    Stall,
    LongStall,
    Freeze,
    Render,
    Watch,
    Crash,
    Abort,
    Terminate,
    Exit,
    Kill,
};

// The buffer a stall fills over and over, the fills of a stall and of a long one, and the frame a
// fault comes at when the variable names none.
constexpr u64 FillBytes = u64{256} << 20;
constexpr u32 StallFills = 2600;
constexpr u32 LongStallFills = 20000;
constexpr u64 DefaultFrame = 2400;

Fault g_fault = Fault::None;
u64 g_frame = DefaultFrame;
Buffer g_fill;
std::atomic<bool> g_made{false};
std::atomic<bool> g_watch_due{false};

// True once, for the first frame at or past the fault's.
bool Due(Fault fault, u64 frame) {
    return g_fault == fault && frame >= g_frame && !g_made.exchange(true);
}

void Say(const char* what) {
    LOG_WARNING(Render_Vulkan, "GR2port catcher test: {} now", what);
    Common::Log::Flush();
}

} // namespace

void Start(Memory& memory) {
    const char* const value = std::getenv("GR2PORT_CATCHER_TEST");
    if (value == nullptr) {
        return;
    }
    const std::string text{value};
    const size_t comma = text.find(',');
    const std::string name = text.substr(0, comma);
    if (comma != std::string::npos) {
        g_frame = std::strtoull(text.c_str() + comma + 1, nullptr, 10);
    }
    constexpr std::pair<const char*, Fault> Names[]{
        {"stall", Fault::Stall},   {"long-stall", Fault::LongStall}, {"freeze", Fault::Freeze},
        {"render", Fault::Render}, {"watch", Fault::Watch},          {"crash", Fault::Crash},
        {"abort", Fault::Abort},   {"terminate", Fault::Terminate},  {"exit", Fault::Exit},
        {"kill", Fault::Kill},
    };
    for (const auto& [known, fault] : Names) {
        if (name == known) {
            g_fault = fault;
        }
    }
    if (g_fault == Fault::Stall || g_fault == Fault::LongStall) {
        g_fill = memory.CreateBuffer(FillBytes, vk::BufferUsageFlagBits::eTransferDst,
                                     Placement::Device);
    }
    LOG_WARNING(Render_Vulkan, "GR2port catcher test: \"{}\" at game frame {}{}", name, g_frame,
                g_fault == Fault::None ? ": no such test" : "");
}

void Game(u64 frame) {
    Crash::Keep();
    if (Due(Fault::Freeze, frame)) {
        Say("the game's draw thread stops for 12 s");
        std::this_thread::sleep_for(std::chrono::seconds{12});
    }
}

void Render(u64 game_frame) {
    if (g_fault == Fault::Watch && game_frame >= g_frame) {
        g_watch_due.store(true, std::memory_order_relaxed);
    }
    if (Due(Fault::Render, game_frame)) {
        Say("the render thread stops for 6 s");
        std::this_thread::sleep_for(std::chrono::seconds{6});
    } else if (Due(Fault::Crash, game_frame)) {
        Say("the render thread writes to address 16");
        *reinterpret_cast<volatile u32*>(u64{16}) = 1;
    } else if (Due(Fault::Abort, game_frame)) {
        Say("the render thread calls abort");
        std::abort();
    } else if (Due(Fault::Terminate, game_frame)) {
        Say("the render thread calls terminate");
        std::terminate();
    } else if (Due(Fault::Exit, game_frame)) {
        Say("the render thread calls exit");
        std::exit(3);
    } else if (Due(Fault::Kill, game_frame)) {
        Say("the process is ended from outside its own code's reach");
#ifdef _WIN32
        TerminateProcess(GetCurrentProcess(), 0xdead);
#else
        std::_Exit(3);
#endif
    }
}

void Gpu(const Device& device, vk::CommandBuffer cmd, u64 game_frame) {
    const bool long_one = g_fault == Fault::LongStall;
    if (!g_fill.buffer || !Due(long_one ? Fault::LongStall : Fault::Stall, game_frame)) {
        return;
    }
    Say(long_one ? "the GPU gets about 10 s of work in the water pass"
                 : "the GPU gets about 1.3 s of work in the water pass");
    const u32 fills = long_one ? LongStallFills : StallFills;
    for (u32 i = 0; i < fills; i++) {
        cmd.fillBuffer(g_fill.buffer, 0, FillBytes, i, device.dispatch);
    }
}

void Watch() {
    if (g_watch_due.load(std::memory_order_relaxed) && !g_made.exchange(true)) {
        Say("the catcher's watch stops for 20 s");
        std::this_thread::sleep_for(std::chrono::seconds{20});
    }
}

} // namespace Gr2Port::Catcher::Test
