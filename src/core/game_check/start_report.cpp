// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <fstream>
#include <string>
#include <vector>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/emulator_settings.h"
#include "core/game_check/start_report.h"
#include "graphics/gr2port/settings.h"
#ifdef _WIN32
#include <windows.h>

#include <shellapi.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#endif

namespace Gr2Port::GameCheck {

namespace {

// The program that started the executable, whether it came from a terminal, where its standard
// output goes when that is a file or a pipe, and its options as given.
std::string g_parent = "an unknown program";
bool g_terminal = false;
std::string g_output;
std::vector<std::string> g_options;

// The sections of config.json that EmulatorSettings holds.
constexpr std::array EmulatorSections{"General", "Network", "Log", "Debug",
                                      "Input",   "Audio",   "GPU", "Vulkan"};

#ifdef _WIN32
std::string ParentName() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return g_parent;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD parent = 0;
    for (BOOL more = Process32FirstW(snapshot, &entry); more && parent == 0;
         more = Process32NextW(snapshot, &entry)) {
        if (entry.th32ProcessID == GetCurrentProcessId()) {
            parent = entry.th32ParentProcessID;
        }
    }
    std::string name = g_parent;
    for (BOOL more = Process32FirstW(snapshot, &entry); more && parent != 0;
         more = Process32NextW(snapshot, &entry)) {
        if (entry.th32ProcessID == parent) {
            name = Common::UTF16ToUTF8(entry.szExeFile);
            break;
        }
    }
    CloseHandle(snapshot);
    return name;
}
#endif

// The options on one line, each with spaces in quotes.
std::string Joined(const std::vector<std::string>& options) {
    std::string line;
    for (const std::string& option : options) {
        if (!line.empty()) {
            line += ' ';
        }
        line += option.find(' ') == std::string::npos ? option : '"' + option + '"';
    }
    return line;
}

} // namespace

void RememberStart([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
#ifdef _WIN32
    g_parent = ParentName();
    // The terminal's console is attached by now, when the executable was started from one.
    g_terminal = GetConsoleWindow() != nullptr;
    switch (GetFileType(GetStdHandle(STD_OUTPUT_HANDLE))) {
    case FILE_TYPE_DISK:
        g_output = "a file";
        break;
    case FILE_TYPE_PIPE:
        g_output = "a pipe";
        break;
    default:
        break;
    }
    // argv is in the ANSI code page; the wide command line holds every name as typed.
    int count = 0;
    if (LPWSTR* const words = CommandLineToArgvW(GetCommandLineW(), &count)) {
        for (int i = 1; i < count; ++i) {
            g_options.push_back(Common::UTF16ToUTF8(words[i]));
        }
        LocalFree(words);
    }
#else
    std::ifstream comm{fmt::format("/proc/{}/comm", getppid())};
    if (std::string name; std::getline(comm, name) && !name.empty()) {
        g_parent = name;
    }
    g_terminal = isatty(STDIN_FILENO) || isatty(STDOUT_FILENO);
    for (int i = 1; i < argc; ++i) {
        g_options.emplace_back(argv[i]);
    }
#endif
}

void LogLaunch() {
    LOG_INFO(Loader, "GR2port: started {} by {}{}",
             g_terminal ? "from a terminal" : "directly, not from a terminal,", g_parent,
             g_output.empty() ? "" : fmt::format(", standard output to {}", g_output));
    LOG_INFO(Loader, "GR2port: options: {}", g_options.empty() ? "none" : Joined(g_options));
}

void LogStart() {
    LogLaunch();
    LOG_INFO(Config, "GR2port: settings GR2port: {}", Settings::Describe());
    for (const char* section : EmulatorSections) {
        LOG_INFO(Config, "GR2port: settings {}: {}", section,
                 EmulatorSettings.GetGroupValues(section).dump(
                     -1, ' ', false, nlohmann::json::error_handler_t::replace));
    }
}

} // namespace Gr2Port::GameCheck
