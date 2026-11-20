// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string_view>

#include <fmt/format.h>

#ifdef _WIN32
#include <windows.h>
// After windows.h.
#include <tlhelp32.h>
#endif

#include "graphics/gr2port/catcher_stacks.h"

namespace Gr2Port::Catcher::Stacks {

namespace {

// The address the game's patches count its code from, and the most its image spans.
constexpr u64 GameLinkBase = 0x400000;
constexpr u64 GameSpan = 0x8000000;

std::atomic<u64> g_game{0};

#ifdef _WIN32

// The calls read of one thread, the stack slots looked through for calls the walk cannot follow,
// the callers guessed from them, and the threads read at most.
constexpr u32 MaxFrames = 28;
constexpr u32 ScanSlots = 384;
constexpr u32 MaxGuesses = 6;
constexpr u32 MaxThreads = 400;
constexpr u32 NameChars = 96;

struct Raw {
    u32 count = 0;
    std::array<u64, MaxFrames> frames{};
    u32 guesses = 0;
    std::array<u64, MaxGuesses> guess{};
    bool faulted = false;
};

// The thread All has stopped, for Rescue.
std::atomic<HANDLE> g_stopped{nullptr};
std::atomic<bool> g_running{false};
std::atomic<bool> g_give_up{false};

bool Query(u64 address, MEMORY_BASIC_INFORMATION& info) {
    return VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) != 0 &&
           info.State == MEM_COMMIT && (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0;
}

bool Readable(u64 address, u64 bytes) {
    MEMORY_BASIC_INFORMATION info;
    return Query(address, info) &&
           address + bytes <= reinterpret_cast<u64>(info.BaseAddress) + info.RegionSize;
}

bool Executable(u64 address) {
    MEMORY_BASIC_INFORMATION info;
    constexpr DWORD Code =
        PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return address > 0x10000 && Query(address, info) && (info.Protect & Code) != 0;
}

// Follows a stopped thread's calls outwards. Code with unwind data is unwound; the game's code
// has none, so there the walk follows the frame pointers while they look like a chain, and then
// lists the stack's words that point into code. Makes no calls that take the heap or the log.
void Walk(CONTEXT& context, Raw& raw) {
    __try {
        bool innermost = true;
        while (raw.count < MaxFrames && context.Rip != 0) {
            raw.frames[raw.count++] = context.Rip;
            DWORD64 image = 0;
            const PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &image, nullptr);
            if (function != nullptr) {
                PVOID handler = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, function, &context,
                                 &handler, &establisher, nullptr);
            } else if (context.Rbp > context.Rsp && context.Rbp - context.Rsp < 0x100000 &&
                       Readable(context.Rbp, 16) &&
                       Executable(*reinterpret_cast<const u64*>(context.Rbp + 8))) {
                const u64* const frame = reinterpret_cast<const u64*>(context.Rbp);
                context.Rip = frame[1];
                context.Rsp = context.Rbp + 16;
                context.Rbp = frame[0];
            } else if (innermost && Readable(context.Rsp, 8) &&
                       Executable(*reinterpret_cast<const u64*>(context.Rsp))) {
                context.Rip = *reinterpret_cast<const u64*>(context.Rsp);
                context.Rsp += 8;
            } else {
                break;
            }
            innermost = false;
        }
        if (raw.count < MaxFrames) {
            for (u32 slot = 0; slot < ScanSlots && raw.guesses < MaxGuesses; slot++) {
                const u64 at = context.Rsp + u64{slot} * 8;
                if (!Readable(at, 8)) {
                    break;
                }
                const u64 word = *reinterpret_cast<const u64*>(at);
                if (word != raw.frames[raw.count - 1] && Executable(word)) {
                    raw.guess[raw.guesses++] = word;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        raw.faulted = true;
    }
}

std::string Narrow(const wchar_t* text) {
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) {
        return {};
    }
    std::string out(static_cast<size_t>(bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), bytes, nullptr, nullptr);
    return out;
}

// The executable's own table of functions, which its link puts in the file after the image:
// each function's place in the image and its name.
struct Function {
    u32 rva;
    u32 name;
};
std::once_flag g_table_once;
std::vector<char> g_table_names;
std::vector<Function> g_table;

void LoadTable() {
    std::array<wchar_t, 1024> path{};
    if (GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size())) == 0) {
        return;
    }
    std::ifstream file{path.data(), std::ios::binary};
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!file.read(reinterpret_cast<char*>(&dos), sizeof(dos)) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE || !file.seekg(dos.e_lfanew) ||
        !file.read(reinterpret_cast<char*>(&nt), sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.PointerToSymbolTable == 0 ||
        nt.FileHeader.NumberOfSymbols == 0) {
        return;
    }
    std::vector<IMAGE_SECTION_HEADER> sections(nt.FileHeader.NumberOfSections);
    file.seekg(dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
               nt.FileHeader.SizeOfOptionalHeader);
    if (!file.read(reinterpret_cast<char*>(sections.data()),
                   static_cast<std::streamsize>(sections.size() * sizeof(IMAGE_SECTION_HEADER)))) {
        return;
    }
    constexpr size_t SymbolBytes = 18;
    std::vector<char> symbols(size_t{nt.FileHeader.NumberOfSymbols} * SymbolBytes);
    file.seekg(nt.FileHeader.PointerToSymbolTable);
    u32 strings_bytes = 0;
    if (!file.read(symbols.data(), static_cast<std::streamsize>(symbols.size())) ||
        !file.read(reinterpret_cast<char*>(&strings_bytes), sizeof(strings_bytes)) ||
        strings_bytes < 4 || strings_bytes > (1u << 30)) {
        return;
    }
    std::vector<char> strings(strings_bytes);
    if (!file.read(strings.data() + 4, strings_bytes - 4)) {
        return;
    }
    // Short names move into the names' block too, each with its end.
    std::vector<char> names = std::move(strings);
    std::vector<Function> table;
    for (u32 i = 0; i < nt.FileHeader.NumberOfSymbols; i++) {
        const char* const symbol = symbols.data() + size_t{i} * SymbolBytes;
        u32 value;
        s16 section;
        std::memcpy(&value, symbol + 8, sizeof(value));
        std::memcpy(&section, symbol + 12, sizeof(section));
        const u8 aux = static_cast<u8>(symbol[17]);
        if (section > 0 && static_cast<size_t>(section) <= sections.size() &&
            (sections[section - 1].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) {
            u32 zero;
            u32 offset;
            std::memcpy(&zero, symbol, sizeof(zero));
            std::memcpy(&offset, symbol + 4, sizeof(offset));
            if (zero != 0 || offset >= names.size()) {
                offset = static_cast<u32>(names.size());
                names.insert(names.end(), symbol, symbol + 8);
                names.push_back('\0');
            }
            table.push_back({sections[section - 1].VirtualAddress + value, offset});
        }
        i += aux;
    }
    names.push_back('\0');
    std::ranges::sort(table, {}, &Function::rva);
    g_table_names = std::move(names);
    g_table = std::move(table);
}

// The function of the executable at rva: its name and rva's distance from its start.
bool InTable(u32 rva, std::string_view& name, u32& into) {
    std::call_once(g_table_once, LoadTable);
    const auto next = std::ranges::upper_bound(g_table, rva, {}, &Function::rva);
    if (next == g_table.begin()) {
        return false;
    }
    const Function& function = *(next - 1);
    name = std::string_view{g_table_names.data() + function.name};
    into = rva - function.rva;
    return true;
}

// The exported function of a loaded file at or before rva.
bool InExports(const u8* module, u32 rva, std::string_view& name, u32& into) {
    __try {
        const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
        const IMAGE_DATA_DIRECTORY& directory =
            nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (directory.VirtualAddress == 0 || directory.Size == 0) {
            return false;
        }
        const auto* const exports =
            reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(module + directory.VirtualAddress);
        const u32* const functions =
            reinterpret_cast<const u32*>(module + exports->AddressOfFunctions);
        const u32* const names = reinterpret_cast<const u32*>(module + exports->AddressOfNames);
        const u16* const ordinals =
            reinterpret_cast<const u16*>(module + exports->AddressOfNameOrdinals);
        u32 best = 0;
        const char* best_name = nullptr;
        for (u32 i = 0; i < exports->NumberOfNames; i++) {
            const u32 start = functions[ordinals[i]];
            if (start <= rva && start >= best) {
                best = start;
                best_name = reinterpret_cast<const char*>(module + names[i]);
            }
        }
        // An export that many bytes before is some other function's.
        if (best_name == nullptr || rva - best > 0x4000) {
            return false;
        }
        name = std::string_view{best_name};
        into = rva - best;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string Describe(const Raw& raw) {
    std::string line;
    for (u32 i = 0; i < raw.count; i++) {
        line += (i != 0 ? " < " : "") + Name(raw.frames[i]);
    }
    if (raw.count == 0) {
        line += "no calls read";
    }
    if (raw.guesses != 0) {
        line += "; the walk ends there, and the stack then holds";
        for (u32 i = 0; i < raw.guesses; i++) {
            line += (i != 0 ? ", " : " ") + Name(raw.guess[i]);
        }
    }
    if (raw.faulted) {
        line += "; the stack's read faulted";
    }
    return line;
}

#endif // _WIN32

} // namespace

void SetGame(VAddr base) {
    g_game.store(base, std::memory_order_relaxed);
}

#ifdef _WIN32

std::string Name(u64 address) {
    const u64 game = g_game.load(std::memory_order_relaxed);
    if (game != 0 && address >= game && address - game < GameSpan) {
        return fmt::format("game {:#x}", address - game + GameLinkBase);
    }
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(address), &module) == 0 ||
        module == nullptr) {
        MEMORY_BASIC_INFORMATION info;
        if (VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) != 0 &&
            info.AllocationBase != nullptr) {
            return fmt::format("{:#x} (in the mapping at {:#x})", address,
                               reinterpret_cast<u64>(info.AllocationBase));
        }
        return fmt::format("{:#x}", address);
    }
    std::array<wchar_t, 1024> path{};
    GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    std::string file = Narrow(path.data());
    if (const size_t slash = file.find_last_of("\\/"); slash != std::string::npos) {
        file.erase(0, slash + 1);
    }
    const bool own = module == GetModuleHandleW(nullptr);
    const u32 rva = static_cast<u32>(address - reinterpret_cast<u64>(module));
    std::string_view name;
    u32 into = 0;
    if (own ? InTable(rva, name, into)
            : InExports(reinterpret_cast<const u8*>(module), rva, name, into)) {
        return fmt::format("{}!{}+{:#x}", own ? "exe" : file, name.substr(0, NameChars), into);
    }
    return fmt::format("{}+{:#x}", own ? "exe" : file, rva);
}

std::string Of(const void* context) {
    Raw raw;
    CONTEXT copy = *static_cast<const CONTEXT*>(context);
    Walk(copy, raw);
    return Describe(raw);
}

std::vector<std::string> All() {
    std::vector<std::string> lines;
    std::call_once(g_table_once, LoadTable);
    std::vector<DWORD> ids;
    const DWORD process = GetCurrentProcessId();
    const DWORD self = GetCurrentThreadId();
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        lines.push_back(fmt::format("the threads cannot be listed: error {}", GetLastError()));
        return lines;
    }
    THREADENTRY32 entry{.dwSize = sizeof(entry)};
    for (BOOL more = Thread32First(snapshot, &entry); more != 0 && ids.size() < MaxThreads;
         more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID == process && entry.th32ThreadID != self) {
            ids.push_back(entry.th32ThreadID);
        }
    }
    CloseHandle(snapshot);
    std::vector<Raw> raws(ids.size());
    std::vector<std::string> names(ids.size());
    std::vector<u8> read(ids.size());
    g_give_up.store(false);
    g_running.store(true);
    for (size_t i = 0; i < ids.size() && !g_give_up.load(); i++) {
        const HANDLE thread = OpenThread(
            THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, ids[i]);
        if (thread == nullptr) {
            continue;
        }
        PWSTR description = nullptr;
        if (SUCCEEDED(GetThreadDescription(thread, &description)) && description != nullptr) {
            names[i] = Narrow(description);
            LocalFree(description);
        }
        // Nothing between the stop and the resume takes the heap or the log: the stopped thread
        // may hold either.
        if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
            g_stopped.store(thread);
            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
            if (GetThreadContext(thread, &context) != 0) {
                Walk(context, raws[i]);
                read[i] = 1;
            }
            g_stopped.store(nullptr);
            ResumeThread(thread);
        }
        CloseHandle(thread);
    }
    g_running.store(false);
    lines.reserve(ids.size() + 1);
    for (size_t i = 0; i < ids.size(); i++) {
        lines.push_back(fmt::format("thread {} \"{}\": {}", ids[i], names[i],
                                    read[i] != 0 ? Describe(raws[i]) : "its stack was not read"));
    }
    if (g_give_up.load()) {
        lines.push_back("the stacks' read was cut short: it took too long");
    }
    return lines;
}

bool Rescue() {
    if (!g_running.load()) {
        return false;
    }
    g_give_up.store(true);
    if (const HANDLE thread = g_stopped.load(); thread != nullptr) {
        ResumeThread(thread);
    }
    return true;
}

bool Named() {
    std::call_once(g_table_once, LoadTable);
    return !g_table.empty();
}

#else

std::string Name(u64 address) {
    const u64 game = g_game.load(std::memory_order_relaxed);
    if (game != 0 && address >= game && address - game < GameSpan) {
        return fmt::format("game {:#x}", address - game + GameLinkBase);
    }
    return fmt::format("{:#x}", address);
}

std::string Of(const void*) {
    return "stacks are not read on this system";
}

std::vector<std::string> All() {
    return {"stacks are not read on this system"};
}

bool Rescue() {
    return false;
}

bool Named() {
    return false;
}

#endif

} // namespace Gr2Port::Catcher::Stacks
