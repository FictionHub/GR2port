// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

namespace Core::FileSys {
class IBackend;
}

// The reference of a good dump, built into the executable: the game's four archives, each with
// the size and the table of contents of its known forms and the files it holds, and the hash of
// the update's eboot.bin as the loader reads it. It comes from dumps of three regions, whose
// archives are the same but for one of the update's. For a title outside its lists, a dump's
// parts are logged alone.
namespace Gr2Port::GameCheck {

/// Logs the size and the hash of the eboot.bin that the root eboot holds. With archives, the dump
/// has the game's four .psarc files in its roots (update is null when it lies over the game):
/// each one's size and table of contents is logged and compared, as is the eboot's hash. Returns
/// the files that differ from the reference of title_id, by name; none for a dump of loose files.
std::vector<std::string> CompareDump(const std::string& title_id, Core::FileSys::IBackend& game,
                                     Core::FileSys::IBackend* update,
                                     Core::FileSys::IBackend& eboot, bool archives);

/// Logs CompareDump's lines once more: the game's log replaces the one they went to first.
void LogDumpAgain();

} // namespace Gr2Port::GameCheck
