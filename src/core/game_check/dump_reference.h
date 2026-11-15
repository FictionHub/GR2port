// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>
#include "common/types.h"

namespace Core::FileSys {
class IBackend;
}

// The reference of a good dump, built into the executable: the game's four archives, each with
// the size, the table of contents and the whole file's hash of its known forms and the files it
// holds, and the hash of the update's eboot.bin as the loader reads it. It comes from dumps of
// three regions, whose archives are the same but for one of the update's. For a title outside its
// lists, a dump's parts are logged alone.
namespace Gr2Port::GameCheck {

/// Logs the size and the hash of the eboot.bin that the root eboot holds. With archives, the dump
/// has the game's four .psarc files in its roots (update is null when it lies over the game):
/// each one's size and table of contents is logged and compared, as is the eboot's hash. Returns
/// the files that differ from the reference of title_id, by name; none for a dump of loose files.
std::vector<std::string> CompareDump(const std::string& title_id, Core::FileSys::IBackend& game,
                                     Core::FileSys::IBackend* update,
                                     Core::FileSys::IBackend& eboot, bool archives);

/// Logs CompareDump's lines, and those of the whole reads since, once more: the game's log
/// replaces the one they went to first.
void LogDumpAgain();

/// One of the game's archives in a read of them whole.
struct WholeArchive {
    enum class State {
        Waiting,
        Reading,
        Good,
        Differs,
        NoReference,
        Unread,
    };

    std::string name;
    u64 size = 0;
    /// The bytes read so far.
    u64 read = 0;
    State state = State::Waiting;
};

/// Reads the game's four .psarc files whole on a thread of its own and compares each one's
/// SHA-256 with the reference of the title, which shows damage inside an archive's data that its
/// size and table of contents hide. Each archive's size, hash and answer is logged. Destroying
/// the object ends the reads.
class WholeRead {
public:
    /// roots are the dump's folders or .zar archives, the update's before the game's.
    WholeRead(std::string title_id, std::vector<std::filesystem::path> roots);

    /// The archives as they stand, in the reference's order.
    std::vector<WholeArchive> Archives() const;

    /// Whether every archive has its answer.
    bool Done() const;

private:
    void Run(const std::stop_token& stop, const std::string& title_id,
             const std::vector<std::filesystem::path>& roots);

    mutable std::mutex mutex;
    std::vector<WholeArchive> archives;
    // The last member: it ends and joins before the others go.
    std::jthread thread;
};

} // namespace Gr2Port::GameCheck
