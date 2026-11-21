// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <fmt/format.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/details/os.h>
#include <spdlog/formatter.h>
#include <spdlog/sinks/base_sink.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "common/types.h"

namespace Common::Log {

/// The most a log file holds.
constexpr u64 MaxLogSize = 8_MB;

/// The log file. Before the game's log begins, lines are added to what the file holds until it
/// reaches its limit. The game's log keeps a start of a quarter of the limit, and past the limit it
/// is cut back to that start, so it holds its start and its latest lines. A line of an error
/// flushes the file, and so does a thread once a second.
class LogFileSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    LogFileSink(const std::filesystem::path& path, u64 size_limit) {
        std::scoped_lock lock{mutex_};
        Open(path, true, size_limit);
    }

    ~LogFileSink() override {
        flusher.request_stop();
        flusher.join();
        if (file != nullptr) {
            std::fclose(file);
        }
    }

    /// Begins the game's log in the file at path, emptied first unless append is set.
    void BeginGameLog(const std::filesystem::path& path, bool append, u64 size_limit) {
        std::scoped_lock lock{mutex_};
        game_log = true;
        Open(path, append, size_limit);
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        Write({formatted.data(), formatted.size()});
        if (msg.log_level >= spdlog::level::err && file != nullptr) {
            std::fflush(file);
        }
    }

    void flush_() override {
        if (file != nullptr) {
            std::fflush(file);
        }
    }

private:
    // Opens the file at path, adding to what it holds when keep is set; the game's log keeps it
    // only while it is at most half of the limit.
    void Open(const std::filesystem::path& path, bool keep, u64 size_limit) {
        if (file != nullptr) {
            std::fclose(file);
        }
        limit = size_limit;
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        const u64 held = std::filesystem::is_regular_file(path, error)
                             ? std::filesystem::file_size(path, error)
                             : 0;
        const bool add = keep && !error && (!game_log || held <= limit / 2);
#ifdef _WIN32
        file = _wfopen(path.c_str(), add ? L"ab" : L"wb");
#else
        file = std::fopen(path.c_str(), add ? "ab" : "wb");
#endif
        size = add ? held : 0;
        start_known = false;
        start = 0;
        cut = 0;
    }

    void Write(std::string_view text) {
        if (file == nullptr || (!game_log && size >= limit)) {
            return;
        }
        std::fwrite(text.data(), 1, text.size(), file);
        const u64 before = size;
        size += text.size();
        if (!game_log) {
            return;
        }
        // The start ends at the end of a line, at most half of the limit into the file.
        if (!start_known && size >= limit / 4) {
            start = size <= limit / 2 ? size : before;
            start_known = true;
        }
        if (size > limit) {
            Cut();
        }
    }

    // Cuts the game's log back to its start and marks the place.
    void Cut() {
        std::fflush(file);
        cut += size - start;
#ifdef _WIN32
        _chsize_s(_fileno(file), static_cast<s64>(start));
#else
        [[maybe_unused]] const int truncated = ftruncate(fileno(file), static_cast<off_t>(start));
#endif
        std::fseek(file, 0, SEEK_END);
        size = start;
        const std::string mark = fmt::format(
            "[Log] <Warning> GR2port: the log is cut here: {:.1f} MB of lines left out to keep it "
            "under {:.1f} MB{}",
            static_cast<double>(cut) / 1_MB, static_cast<double>(limit) / 1_MB,
            spdlog::details::os::default_eol);
        std::fwrite(mark.data(), 1, mark.size(), file);
        size += mark.size();
    }

    std::FILE* file = nullptr;
    bool game_log = false;
    u64 limit = MaxLogSize;
    // The bytes the file holds, the end of the game's log's start, and the bytes cut so far.
    u64 size = 0;
    bool start_known = false;
    u64 start = 0;
    u64 cut = 0;

    // Declared last, so it starts after the members it reads.
    std::jthread flusher{[this](std::stop_token stop) {
        std::mutex wait_mutex;
        std::condition_variable_any wake;
        std::unique_lock wait_lock{wait_mutex};
        while (!stop.stop_requested()) {
            wake.wait_for(wait_lock, stop, std::chrono::seconds{1}, [] { return false; });
            flush();
        }
    }};
};

} // namespace Common::Log
