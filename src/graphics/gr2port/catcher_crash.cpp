// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <new>
#include <string>

#include <fmt/format.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "common/logging/log.h"
#include "common/types.h"
#include "graphics/gr2port/catcher_crash.h"
#include "graphics/gr2port/catcher_note.h"
#include "graphics/gr2port/catcher_outside.h"
#include "graphics/gr2port/catcher_stacks.h"

namespace Gr2Port::Catcher::Crash {

namespace {

// The first death's word is the one that counts; a second one while it is written says so only.
std::atomic<u32> g_words{0};
std::terminate_handler g_terminate = nullptr;
std::new_handler g_new = nullptr;
// The abort handler other code installed after this one.
using SignalHandler = void (*)(int);
std::atomic<SignalHandler> g_abort_before{nullptr};

constexpr const char* WordsFile = "catcher_last_words.txt";

#ifdef _WIN32

LPTOP_LEVEL_EXCEPTION_FILTER g_filter = nullptr;
// The last words' own file, written past the log: a dying process may have lost its log already.
HANDLE g_raw = INVALID_HANDLE_VALUE;

void Raw(std::string_view line) {
    if (g_raw == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(g_raw, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    WriteFile(g_raw, "\r\n", 2, &written, nullptr);
}

void OpenRaw(const std::filesystem::path& path) {
    g_raw = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
}

const char* CodeName(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
        return "access violation";
    case EXCEPTION_STACK_OVERFLOW:
        return "stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "illegal instruction";
    case EXCEPTION_PRIV_INSTRUCTION:
        return "privileged instruction";
    case EXCEPTION_IN_PAGE_ERROR:
        return "a page could not be read from disk";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "integer division by zero";
    case EXCEPTION_BREAKPOINT:
        return "breakpoint";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return "array bounds exceeded";
    case 0xC0000409:
        return "fail fast (a check in the runtime or a library ended the process)";
    case 0xC0000374:
        return "heap corruption";
    case 0xE06D7363:
        return "a C++ exception nothing caught";
    default:
        return "an exception";
    }
}

std::string OwnStack() {
    CONTEXT context{};
    RtlCaptureContext(&context);
    return Stacks::Of(&context);
}

std::string ThreadName() {
    PWSTR description = nullptr;
    std::string name;
    if (SUCCEEDED(GetThreadDescription(GetCurrentThread(), &description)) &&
        description != nullptr) {
        for (const wchar_t* c = description; *c != 0; c++) {
            name.push_back(*c < 0x80 ? static_cast<char>(*c) : '?');
        }
        LocalFree(description);
    }
    return fmt::format("thread {} \"{}\"", GetCurrentThreadId(), name);
}

#else

void Raw(std::string_view) {}

void OpenRaw(const std::filesystem::path&) {}

std::string OwnStack() {
    return "stacks are not read on this system";
}

std::string ThreadName() {
    return "a thread";
}

#endif

// A last word goes to its own file first, then to the log.
void Last(const std::string& line) {
    Raw(line);
    LOG_CRITICAL(Render_Vulkan, "GR2port catcher: {}", line);
}

void Others() {
    for (const std::string& line : History()) {
        Last("before the death: " + line);
    }
    Common::Log::Flush();
    for (const std::string& line : Stacks::All()) {
        Last("at the death: " + line);
    }
    Common::Log::Flush();
}

#ifdef _WIN32

LONG WINAPI Filter(EXCEPTION_POINTERS* pointers) {
    const EXCEPTION_RECORD& record = *pointers->ExceptionRecord;
    if (g_words.fetch_add(1) == 0) {
        Outside::Set(Outside::Crumb::LastWord, 1);
        std::string what =
            fmt::format("{:#x}, {}", record.ExceptionCode, CodeName(record.ExceptionCode));
        if ((record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
             record.ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
            record.NumberParameters >= 2) {
            const ULONG_PTR kind = record.ExceptionInformation[0];
            what += fmt::format(" {} {:#x}",
                                kind == 0   ? "reading"
                                : kind == 1 ? "writing"
                                            : "executing",
                                record.ExceptionInformation[1]);
        }
        const CONTEXT& c = *pointers->ContextRecord;
        Last(fmt::format(
            "{} dies of {} at {}. Registers: rax {:#x} rbx {:#x} rcx {:#x} rdx {:#x} rsi {:#x} "
            "rdi {:#x} rbp {:#x} rsp {:#x} r8 {:#x} r9 {:#x} r10 {:#x} r11 {:#x} r12 {:#x} r13 "
            "{:#x} r14 {:#x} r15 {:#x}",
            ThreadName(), what, Stacks::Name(reinterpret_cast<u64>(record.ExceptionAddress)), c.Rax,
            c.Rbx, c.Rcx, c.Rdx, c.Rsi, c.Rdi, c.Rbp, c.Rsp, c.R8, c.R9, c.R10, c.R11, c.R12, c.R13,
            c.R14, c.R15));
        Common::Log::Flush();
        // A thread out of stack has no room for more.
        if (record.ExceptionCode != EXCEPTION_STACK_OVERFLOW) {
            Last("its calls: " + Stacks::Of(pointers->ContextRecord));
            Common::Log::Flush();
            Others();
        }
    }
    return g_filter != nullptr ? g_filter(pointers) : EXCEPTION_CONTINUE_SEARCH;
}

#endif

// The last word of a death that is a call: what, from where.
void Word(const char* what) {
    if (g_words.fetch_add(1) != 0) {
        return;
    }
    Outside::Set(Outside::Crumb::LastWord, 1);
    Last(fmt::format("{} {}. Its calls: {}", ThreadName(), what, OwnStack()));
    Common::Log::Flush();
    Others();
}

void OnTerminate() {
    Word("called terminate: an exception nothing caught, or a thread destroyed while it ran");
    if (g_terminate != nullptr) {
        g_terminate();
    }
    std::abort();
}

void OnAbort(int signal) {
    Word("called abort");
    if (const SignalHandler before = g_abort_before.load(); before != nullptr) {
        before(signal);
    }
    std::signal(SIGABRT, SIG_DFL);
    std::abort();
}

void OnNoMemory() {
    Word("asked for memory the system did not give");
    if (g_new != nullptr) {
        g_new();
        return;
    }
    std::abort();
}

void OnExit() {
    // An exit is not a death, and its word does not silence one.
    const std::string line =
        fmt::format("{} ends the process in order. Its calls: {}", ThreadName(), OwnStack());
    Raw(line);
    LOG_INFO(Render_Vulkan, "GR2port catcher: {}", line);
    Common::Log::Flush();
}

#ifdef _WIN32

void OnBadArgument(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
    Word("gave the C runtime an argument it refuses");
    std::abort();
}

void OnPureCall() {
    Word("called a function of an object that was being built or destroyed");
    std::abort();
}

#endif

} // namespace

void Keep() {
    const SignalHandler before = std::signal(SIGABRT, OnAbort);
    if (before != OnAbort && before != SIG_DFL && before != SIG_IGN && before != SIG_ERR) {
        g_abort_before.store(before);
    }
    thread_local bool kept = false;
    if (!kept) {
        kept = true;
        std::set_terminate(OnTerminate);
    }
}

void Install(const std::filesystem::path& log_folder) {
    // The last run's last words, which its log may lack.
    {
        std::ifstream last{log_folder / WordsFile};
        std::string line;
        u32 lines = 0;
        constexpr u32 MaxLines = 600;
        while (lines < MaxLines && std::getline(last, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (lines++ == 0) {
                LOG_WARNING(Render_Vulkan,
                            "GR2port catcher: the last run left last words in their own file:");
            }
            LOG_WARNING(Render_Vulkan, "GR2port catcher: the last run: {}", line);
        }
    }
    OpenRaw(log_folder / WordsFile);
    g_terminate = std::set_terminate(OnTerminate);
    g_new = std::set_new_handler(OnNoMemory);
    std::signal(SIGABRT, OnAbort);
    std::atexit(OnExit);
#ifdef _WIN32
    g_filter = SetUnhandledExceptionFilter(Filter);
    _set_invalid_parameter_handler(OnBadArgument);
    _set_purecall_handler(OnPureCall);
#endif
    LOG_INFO(Render_Vulkan,
             "GR2port catcher: the process's ways to die each leave a last word, in the log and "
             "in {}",
             WordsFile);
}

} // namespace Gr2Port::Catcher::Crash
