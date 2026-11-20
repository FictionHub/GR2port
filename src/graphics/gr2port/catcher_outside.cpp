// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <fmt/xchar.h>

#ifdef _WIN32
#include <windows.h>
// After windows.h.
#include <dbghelp.h>
#include <psapi.h>
#include <tlhelp32.h>
#endif

#include "graphics/gr2port/catcher_outside.h"
#include "graphics/gr2port/catcher_stacks.h"
#include "graphics/gr2port/catcher_system.h"

namespace Gr2Port::Catcher::Outside {

#ifdef _WIN32

namespace {

constexpr u32 Crumbs = static_cast<u32>(Crumb::Count);
constexpr std::array<const char*, Crumbs> CrumbNames{
    "the watch last ran",       "the watch's guard last ran", "the game's code is at",
    "game frames started",      "the render thread's call",   "a last word was logged",
    "the GPU had a late frame",
};

// What the two processes share. Each value has the time it was set, in Windows's milliseconds.
struct Shared {
    u32 size;
    std::array<std::atomic<u64>, Crumbs> values;
    std::array<std::atomic<u64>, Crumbs> times;
    std::array<wchar_t, 520> log_folder;
};

// The outside watcher looks this often; it reads the stacks when both watchers inside have been
// silent for WatchersSilent, or the window has not answered for WindowSilent.
constexpr DWORD LookMs = 500;
constexpr u64 WatchersSilent = 15000;
constexpr u64 WindowSilent = 8000;
constexpr u32 MaxFrames = 28;

Shared* g_shared = nullptr;

std::wstring MappingName(u32 process) {
    return fmt::format(L"Local\\GR2portCatcher-{}", process);
}

std::string Narrow(std::wstring_view text) {
    std::string out;
    for (const wchar_t c : text) {
        out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    }
    return out;
}

const char* ExitMeaning(DWORD code) {
    switch (code) {
    case 0:
        return "an orderly end";
    case 1:
        return "ended from outside, as Task Manager does, or a plain failure";
    case 3:
        return "abort";
    case 0xdead:
        return "the catcher's own kill test";
    case 0xffffffff:
        return "ended from outside by another program";
    case 0xC0000005:
        return "access violation that nothing handled";
    case 0xC00000FD:
        return "stack overflow";
    case 0xC0000409:
        return "fail fast: a check in the runtime or a library ended the process";
    case 0xC0000374:
        return "heap corruption";
    case 0xC000013A:
        return "closed from its console";
    case 0xC0000135:
        return "a library it needs was not found";
    case 0xC0000142:
        return "a library failed to start";
    case 0xC000001D:
        return "illegal instruction";
    case 0x40010004:
        return "ended by a debugger";
    default:
        return "no name known here";
    }
}

struct Writer {
    std::FILE* file = nullptr;
    void Line(const std::string& text) {
        if (file == nullptr) {
            return;
        }
        SYSTEMTIME time;
        GetLocalTime(&time);
        fmt::print(file, "{:02}:{:02}:{:02}.{:03} {}\n", time.wHour, time.wMinute, time.wSecond,
                   time.wMilliseconds, text);
        std::fflush(file);
    }
};

void WriteCrumbs(Writer& out) {
    const u64 now = GetTickCount64();
    for (u32 i = 0; i < Crumbs; i++) {
        const u64 time = g_shared->times[i].load();
        out.Line(time == 0 ? fmt::format("  {}: never set", CrumbNames[i])
                           : fmt::format("  {}: {:#x}, set {} ms before", CrumbNames[i],
                                         g_shared->values[i].load(), now - time));
    }
}

// A module of the game's process.
struct Module {
    u64 base;
    u64 size;
    std::string file;
};

std::vector<Module> Modules(HANDLE process) {
    std::vector<Module> modules;
    std::array<HMODULE, 1024> handles{};
    DWORD bytes = 0;
    if (K32EnumProcessModulesEx(process, handles.data(), sizeof(handles), &bytes,
                                LIST_MODULES_64BIT) == 0) {
        return modules;
    }
    for (size_t i = 0; i < std::min<size_t>(bytes / sizeof(HMODULE), handles.size()); i++) {
        MODULEINFO info{};
        std::array<wchar_t, 1024> path{};
        if (K32GetModuleInformation(process, handles[i], &info, sizeof(info)) == 0 ||
            K32GetModuleFileNameExW(process, handles[i], path.data(),
                                    static_cast<DWORD>(path.size())) == 0) {
            continue;
        }
        std::wstring_view file{path.data()};
        if (const size_t slash = file.find_last_of(L"\\/"); slash != file.npos) {
            file.remove_prefix(slash + 1);
        }
        modules.push_back(
            {reinterpret_cast<u64>(info.lpBaseOfDll), info.SizeOfImage, Narrow(file)});
    }
    return modules;
}

// The name of an address of the game's process. This process is the same executable at the same
// place, and holds Windows's own files at the same places too, so its own names serve for those.
std::string RemoteName(u64 address, const std::vector<Module>& modules) {
    const u64 game = g_shared->values[static_cast<u32>(Crumb::GameBase)].load();
    if (game != 0) {
        Stacks::SetGame(game);
    }
    for (const Module& module : modules) {
        if (address < module.base || address - module.base >= module.size) {
            continue;
        }
        HMODULE own = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(address), &own) != 0 &&
            reinterpret_cast<u64>(own) == module.base) {
            return Stacks::Name(address);
        }
        return fmt::format("{}+{:#x}", module.file, address - module.base);
    }
    if (game != 0 && address >= game && address - game < 0x8000000) {
        return Stacks::Name(address);
    }
    return fmt::format("{:#x}", address);
}

// Every thread's calls, read from outside with Windows's debugging library.
void WriteStacks(Writer& out, HANDLE process, u32 process_id) {
    const HMODULE library = LoadLibraryW(L"dbghelp.dll");
    const auto find = [&](const char* name) { return GetProcAddress(library, name); };
    const auto options = reinterpret_cast<decltype(&SymSetOptions)>(find("SymSetOptions"));
    const auto initialize = reinterpret_cast<decltype(&SymInitialize)>(find("SymInitialize"));
    const auto walk = reinterpret_cast<decltype(&StackWalk64)>(find("StackWalk64"));
    const auto table =
        reinterpret_cast<decltype(&SymFunctionTableAccess64)>(find("SymFunctionTableAccess64"));
    const auto base = reinterpret_cast<decltype(&SymGetModuleBase64)>(find("SymGetModuleBase64"));
    const auto cleanup = reinterpret_cast<decltype(&SymCleanup)>(find("SymCleanup"));
    if (library == nullptr || options == nullptr || initialize == nullptr || walk == nullptr ||
        table == nullptr || base == nullptr || cleanup == nullptr) {
        out.Line("Windows's debugging library is not there: no stacks from outside");
        return;
    }
    options(SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
    if (initialize(process, nullptr, TRUE) == 0) {
        out.Line(fmt::format("the game's modules cannot be read: error {}", GetLastError()));
        return;
    }
    const std::vector<Module> modules = Modules(process);
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry{.dwSize = sizeof(entry)};
    u32 threads = 0;
    for (BOOL more = Thread32First(snapshot, &entry); more != 0;
         more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != process_id) {
            continue;
        }
        const HANDLE thread =
            OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
                       entry.th32ThreadID);
        if (thread == nullptr) {
            continue;
        }
        threads++;
        std::string name;
        PWSTR description = nullptr;
        if (SUCCEEDED(GetThreadDescription(thread, &description)) && description != nullptr) {
            name = Narrow(description);
            LocalFree(description);
        }
        std::array<u64, MaxFrames> frames{};
        u32 count = 0;
        if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
            if (GetThreadContext(thread, &context) != 0) {
                STACKFRAME64 frame{};
                frame.AddrPC = {.Offset = context.Rip, .Mode = AddrModeFlat};
                frame.AddrFrame = {.Offset = context.Rbp, .Mode = AddrModeFlat};
                frame.AddrStack = {.Offset = context.Rsp, .Mode = AddrModeFlat};
                while (count < MaxFrames &&
                       walk(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr,
                            table, base, nullptr) != 0 &&
                       frame.AddrPC.Offset != 0) {
                    frames[count++] = frame.AddrPC.Offset;
                }
            }
            ResumeThread(thread);
        }
        CloseHandle(thread);
        std::string line = fmt::format("  thread {} \"{}\":", entry.th32ThreadID, name);
        for (u32 i = 0; i < count; i++) {
            line += (i != 0 ? " < " : " ") + RemoteName(frames[i], modules);
        }
        out.Line(count != 0 ? line : line + " its stack was not read");
    }
    CloseHandle(snapshot);
    cleanup(process);
    out.Line(fmt::format("{} threads read", threads));
}

struct WindowSearch {
    DWORD process;
    HWND window;
};

BOOL CALLBACK FindWindowOf(HWND window, LPARAM parameter) {
    auto* const search = reinterpret_cast<WindowSearch*>(parameter);
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process == search->process && IsWindowVisible(window) != 0 &&
        GetWindow(window, GW_OWNER) == nullptr) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

} // namespace

void Start(const std::filesystem::path& log_folder) {
    const HANDLE mapping =
        CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared),
                           MappingName(GetCurrentProcessId()).c_str());
    if (mapping == nullptr) {
        return;
    }
    g_shared =
        static_cast<Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
    if (g_shared == nullptr) {
        return;
    }
    g_shared->size = sizeof(Shared);
    const std::wstring folder = log_folder.wstring();
    folder.copy(g_shared->log_folder.data(), g_shared->log_folder.size() - 1);
    std::array<wchar_t, 1024> exe{};
    GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    std::wstring command =
        fmt::format(L"\"{}\" {} {}", exe.data(), L"--catcher-outside", GetCurrentProcessId());
    STARTUPINFOW startup{.cb = sizeof(startup)};
    PROCESS_INFORMATION started{};
    if (CreateProcessW(exe.data(), command.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &startup,
                       &started) != 0) {
        CloseHandle(started.hThread);
        CloseHandle(started.hProcess);
    }
}

void Set(Crumb crumb, u64 value) {
    if (g_shared == nullptr) {
        return;
    }
    const u32 index = static_cast<u32>(crumb);
    g_shared->values[index].store(value, std::memory_order_relaxed);
    g_shared->times[index].store(GetTickCount64(), std::memory_order_relaxed);
}

int Run(u32 process_id) {
    HANDLE mapping = nullptr;
    for (u32 tries = 0; tries < 50 && mapping == nullptr; tries++) {
        mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, MappingName(process_id).c_str());
        if (mapping == nullptr) {
            Sleep(100);
        }
    }
    const HANDLE process =
        OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, process_id);
    if (mapping == nullptr || process == nullptr) {
        return 1;
    }
    g_shared = static_cast<Shared*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(Shared)));
    if (g_shared == nullptr || g_shared->size != sizeof(Shared)) {
        return 1;
    }
    Writer out;
    const std::wstring path = std::wstring{g_shared->log_folder.data()} + L"\\catcher_outside.txt";
    _wfopen_s(&out.file, path.c_str(), L"w");
    out.Line(fmt::format("GR2port catcher, the watcher outside the game: watching process {}",
                         process_id));
    const u64 started = GetTickCount64();
    u64 window_answered = started;
    bool window_reported = false;
    bool watchers_reported = false;
    for (;;) {
        if (WaitForSingleObject(process, LookMs) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(process, &code);
            const bool word = g_shared->times[static_cast<u32>(Crumb::LastWord)].load() != 0;
            out.Line(fmt::format(
                "the game's process ended with code {:#x}: {}. {}", code, ExitMeaning(code),
                word || code == 0
                    ? "Its log has its own account"
                    : "It left no last word in its log: it was ended at once, by something "
                      "outside its code, by Windows, or by a fault with no stack left to "
                      "report on"));
            out.Line("what the game last told this watcher:");
            WriteCrumbs(out);
            for (const std::string& line : System::AtEvent(15)) {
                out.Line(line);
            }
            return 0;
        }
        const u64 now = GetTickCount64();
        const u64 watch = g_shared->times[static_cast<u32>(Crumb::WatchBeat)].load();
        const u64 guard = g_shared->times[static_cast<u32>(Crumb::GuardBeat)].load();
        const bool watchers_silent = watch != 0 && now - std::max(watch, guard) > WatchersSilent;
        WindowSearch search{.process = process_id, .window = nullptr};
        EnumWindows(FindWindowOf, reinterpret_cast<LPARAM>(&search));
        DWORD_PTR result = 0;
        if (search.window == nullptr || SendMessageTimeoutW(search.window, WM_NULL, 0, 0,
                                                            SMTO_ABORTIFHUNG, 1000, &result) != 0) {
            window_answered = now;
            window_reported = false;
        }
        const bool window_silent = now - window_answered > WindowSilent;
        if ((watchers_silent && !watchers_reported) || (window_silent && !window_reported)) {
            out.Line(watchers_silent && !watchers_reported
                         ? fmt::format("both of the game's own watchers have been silent for {} ms",
                                       now - std::max(watch, guard))
                         : fmt::format("the game's window has not answered Windows for {} ms",
                                       now - window_answered));
            watchers_reported = watchers_reported || watchers_silent;
            window_reported = window_reported || window_silent;
            out.Line("what the game last told this watcher:");
            WriteCrumbs(out);
            out.Line("every thread's calls, read from outside:");
            WriteStacks(out, process, process_id);
            for (const std::string& line : System::AtEvent(15)) {
                out.Line(line);
            }
        }
        if (!watchers_silent) {
            watchers_reported = false;
        }
    }
}

#else

void Start(const std::filesystem::path&) {}

void Set(Crumb, u64) {}

int Run(u32) {
    return 0;
}

#endif

} // namespace Gr2Port::Catcher::Outside
