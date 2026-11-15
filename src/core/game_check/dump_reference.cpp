// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <charconv>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <cmrc/cmrc.hpp>
#include <fmt/format.h>
#include <openssl/evp.h>
#include <zlib.h>

#include "common/io_file.h"
#include "common/logging/log.h"
#include "common/types.h"
#include "core/file_sys/fs.h"
#include "core/file_sys/ifile.h"
#include "core/game_check/dump_reference.h"
#include "core/loader/elf.h"

CMRC_DECLARE(gr2port_dumps);

namespace Gr2Port::GameCheck {

namespace {

using Core::FileSys::IBackend;
using Core::FileSys::IFile;

constexpr const char* ReferenceFile = "src/resources/dumps/gravity_rush_2.txt";
// The files a result names for the player; the log names them all up to LoggedFiles.
constexpr size_t NamedFiles = 6;
constexpr size_t LoggedFiles = 40;
// A table of contents larger than this is not one of the game's.
constexpr u64 MaxTocBytes = 64ull << 20;
// PSARC header: magic, then big-endian words; the table of contents' length covers the header, the
// entries and the block sizes.
constexpr u64 HeaderBytes = 32;
constexpr u64 HeaderTocLength = 12;
constexpr u64 HeaderEntrySize = 16;
constexpr u64 HeaderEntries = 20;
constexpr u64 HeaderBlockSize = 24;
// Entry: the name's MD5, the first block's index, then the size and the place as 40-bit words.
constexpr u64 MinEntryBytes = 30;
constexpr u64 DigestBytes = 16;
constexpr u64 EntryFirstBlock = 16;
constexpr u64 EntrySize = 20;
constexpr u64 EntryOffset = 25;
// The bytes a whole read of an archive takes at a time.
constexpr u64 WholeReadBytes = 4ull << 20;

// A known build of an archive, or of the eboot: its size where it has one, its hash, an
// archive's hash as a whole file and the titles dumped with it.
struct Form {
    u64 size = 0;
    std::string_view hash;
    std::string_view whole;
    std::vector<std::string_view> titles;
};

struct Archive {
    std::string_view name;
    std::vector<Form> forms;
    // The file lines: sizes, a space, the MD5 of the path.
    std::string_view files;
};

struct Reference {
    std::vector<Archive> archives;
    std::vector<Form> eboots;
};

// A line about the dump, and whether it tells of a problem.
struct Line {
    bool problem = false;
    std::string text;
};
std::vector<Line> g_lines;

void Write(const Line& line) {
    if (line.problem) {
        LOG_ERROR(Loader, "GR2port: dump: {}", line.text);
    } else {
        LOG_INFO(Loader, "GR2port: dump: {}", line.text);
    }
}

// Logs a line about the dump, as a problem or as a fact, and keeps it for LogDumpAgain.
template <typename... Args>
void Log(bool problem, fmt::format_string<Args...> format, Args&&... args) {
    g_lines.push_back({problem, fmt::format(format, std::forward<Args>(args)...)});
    Write(g_lines.back());
}

u64 Number(std::string_view text) {
    u64 value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

// Calls line for each line of text, without its end.
template <typename F>
void ForEachLine(std::string_view text, F&& line) {
    while (!text.empty()) {
        const size_t end = text.find('\n');
        line(text.substr(0, end));
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
    }
}

Reference Parse(std::string_view text) {
    Reference reference;
    Form* form = nullptr;
    ForEachLine(text, [&](std::string_view line) {
        if (line.empty() || line.front() == '#') {
            return;
        }
        const size_t space = line.find(' ');
        const std::string_view word = line.substr(0, space);
        const std::string_view rest =
            space == std::string_view::npos ? std::string_view{} : line.substr(space + 1);
        if (word == "archive") {
            reference.archives.push_back({.name = rest});
        } else if (word == "eboot") {
            form = &reference.eboots.emplace_back(Form{.hash = rest});
        } else if (word == "form" && !reference.archives.empty()) {
            const size_t split = rest.find(' ');
            form = &reference.archives.back().forms.emplace_back(Form{
                .size = Number(rest.substr(0, split)),
                .hash = split == std::string_view::npos ? rest : rest.substr(split + 1),
            });
        } else if (word == "whole" && form != nullptr) {
            form->whole = rest;
        } else if (word == "titles" && form != nullptr) {
            for (std::string_view titles = rest; !titles.empty();) {
                const size_t end = titles.find(' ');
                form->titles.push_back(titles.substr(0, end));
                titles.remove_prefix(end == std::string_view::npos ? titles.size() : end + 1);
            }
        } else if (!reference.archives.empty()) {
            // The file lines of an archive follow one another.
            std::string_view& files = reference.archives.back().files;
            const char* const begin = files.empty() ? line.data() : files.data();
            files = std::string_view{begin, static_cast<size_t>(line.data() + line.size() - begin)};
        }
    });
    return reference;
}

const Reference& TheReference() {
    static const Reference reference = [] {
        const cmrc::file file = cmrc::gr2port_dumps::get_filesystem().open(ReferenceFile);
        return Parse(std::string_view{file.begin(), file.size()});
    }();
    return reference;
}

// The index of the form that lists title, or none.
std::optional<size_t> FormOf(const std::vector<Form>& forms, std::string_view title) {
    for (size_t i = 0; i < forms.size(); ++i) {
        if (std::ranges::find(forms[i].titles, title) != forms[i].titles.end()) {
            return i;
        }
    }
    return std::nullopt;
}

// Calls file with each file line's name digest and its size in the form of that index.
template <typename F>
void ForEachFile(const Archive& archive, size_t form, F&& file) {
    ForEachLine(archive.files, [&](std::string_view line) {
        const size_t space = line.find(' ');
        if (space == std::string_view::npos) {
            return;
        }
        // One size for every form, or one for each.
        std::string_view sizes = line.substr(0, space);
        for (size_t i = 0; i < form && sizes.find(',') != std::string_view::npos; ++i) {
            sizes.remove_prefix(sizes.find(',') + 1);
        }
        file(line.substr(space + 1), Number(sizes.substr(0, sizes.find(','))));
    });
}

std::string Hex(std::span<const u8> bytes) {
    static constexpr std::string_view Digits = "0123456789abcdef";
    std::string hex;
    hex.reserve(bytes.size() * 2);
    for (const u8 byte : bytes) {
        hex += Digits[byte >> 4];
        hex += Digits[byte & 15];
    }
    return hex;
}

// A hash of that kind over the bytes added to it.
class Hash {
public:
    explicit Hash(const EVP_MD* kind) : context{EVP_MD_CTX_new()} {
        EVP_DigestInit_ex(context, kind, nullptr);
    }
    ~Hash() {
        EVP_MD_CTX_free(context);
    }
    Hash(const Hash&) = delete;
    Hash& operator=(const Hash&) = delete;

    void Add(std::span<const u8> bytes) {
        EVP_DigestUpdate(context, bytes.data(), bytes.size());
    }

    // The hash in hex; the object takes no bytes after it.
    std::string Finish() {
        std::array<u8, EVP_MAX_MD_SIZE> digest;
        unsigned int length = 0;
        EVP_DigestFinal_ex(context, digest.data(), &length);
        return Hex(std::span{digest}.first(length));
    }

private:
    EVP_MD_CTX* context;
};

// The MD5 of a file's name, in hex: an archive's entry holds it.
std::string NameDigest(std::string_view name) {
    Hash hash{EVP_md5()};
    hash.Add({reinterpret_cast<const u8*>(name.data()), name.size()});
    return hash.Finish();
}

bool ReadAt(IFile& file, u64 offset, std::span<u8> bytes) {
    return file.Seek(static_cast<s64>(offset), Common::FS::SeekOrigin::SetOrigin) &&
           file.Read(bytes.data(), bytes.size()) == static_cast<s64>(bytes.size());
}

u64 BigEndian(std::span<const u8> bytes) {
    u64 value = 0;
    for (const u8 byte : bytes) {
        value = value << 8 | byte;
    }
    return value;
}

// An archive's table of contents as it is in the file.
struct Toc {
    std::vector<u8> bytes;
    u64 entry_size = 0;
    u64 entries = 0;
    u64 block_size = 0;
};

std::optional<Toc> ReadToc(IFile& file) {
    std::array<u8, HeaderBytes> header;
    if (!ReadAt(file, 0, header) ||
        std::string_view{reinterpret_cast<char*>(header.data()), 4} != "PSAR") {
        return std::nullopt;
    }
    const std::span<const u8> words{header};
    Toc toc{
        .entry_size = BigEndian(words.subspan(HeaderEntrySize, 4)),
        .entries = BigEndian(words.subspan(HeaderEntries, 4)),
        .block_size = BigEndian(words.subspan(HeaderBlockSize, 4)),
    };
    const u64 length = BigEndian(words.subspan(HeaderTocLength, 4));
    if (length > std::min(file.Size(), MaxTocBytes) || toc.entry_size < MinEntryBytes ||
        toc.entries == 0 || toc.block_size == 0 ||
        HeaderBytes + toc.entries * toc.entry_size > length) {
        return std::nullopt;
    }
    toc.bytes.resize(length);
    if (!ReadAt(file, 0, toc.bytes)) {
        return std::nullopt;
    }
    return toc;
}

// The entry of that index in a table of contents.
std::span<const u8> Entry(const Toc& toc, u64 index) {
    return std::span<const u8>{toc.bytes}.subspan(HeaderBytes + index * toc.entry_size,
                                                  toc.entry_size);
}

// The names of an archive's files, in the order of its entries after the first. That entry holds
// them, a line each, in blocks that zlib packs where that makes them smaller. Empty when the names
// cannot be read.
std::vector<std::string> ReadNames(IFile& file, const Toc& toc) {
    const std::span<const u8> bytes{toc.bytes};
    const u64 size_bytes = toc.block_size <= 0x10000 ? 2 : toc.block_size <= 0x1000000 ? 3 : 4;
    const u64 sizes_at = HeaderBytes + toc.entries * toc.entry_size;
    const u64 blocks = (bytes.size() - sizes_at) / size_bytes;
    u64 block = BigEndian(Entry(toc, 0).subspan(EntryFirstBlock, 4));
    u64 left = BigEndian(Entry(toc, 0).subspan(EntrySize, 5));
    u64 at = BigEndian(Entry(toc, 0).subspan(EntryOffset, 5));
    if (left > MaxTocBytes) {
        return {};
    }
    std::string names;
    for (; left != 0; ++block) {
        if (block >= blocks) {
            return {};
        }
        // A block size of 0 stands for a whole block as it is.
        const u64 stored = BigEndian(bytes.subspan(sizes_at + block * size_bytes, size_bytes));
        const u64 packed = stored != 0 ? stored : toc.block_size;
        std::vector<u8> data(packed);
        if (!ReadAt(file, at, data)) {
            return {};
        }
        u64 plain_bytes = packed;
        if (packed == toc.block_size || packed == left) {
            names.append(reinterpret_cast<const char*>(data.data()), data.size());
        } else {
            std::vector<u8> plain(std::min(left, toc.block_size));
            uLongf length = static_cast<uLongf>(plain.size());
            if (uncompress(plain.data(), &length, data.data(), static_cast<uLong>(data.size())) !=
                    Z_OK ||
                length == 0) {
                return {};
            }
            names.append(reinterpret_cast<const char*>(plain.data()), length);
            plain_bytes = length;
        }
        at += packed;
        left -= std::min(left, plain_bytes);
    }
    std::vector<std::string> list;
    ForEachLine(names, [&](std::string_view name) { list.emplace_back(name); });
    return list;
}

// What a comparison of files found: the reference's files that are missing or of another size,
// and the names the archive gives the first of the latter.
struct Difference {
    u64 missing = 0;
    u64 resized = 0;
    std::vector<std::string> names;
};

// A file of the reference: its size, and whether the archive holds it.
struct Expected {
    u64 size = 0;
    bool found = false;
};

// Compares an archive's entries, by their name digests and sizes, with the files of the form of
// that index, and logs the first that differ. A file of another size goes by the name the archive
// gives it; a missing one has its digest alone.
Difference Compare(IFile& file, const Toc& toc, const Archive& archive, size_t form) {
    std::unordered_map<std::string_view, Expected> expected;
    ForEachFile(archive, form,
                [&](std::string_view digest, u64 size) { expected[digest] = {size}; });
    const std::vector<std::string> names = ReadNames(file, toc);
    Difference difference;
    const auto logged = [&] { return difference.missing + difference.resized <= LoggedFiles; };
    for (u64 index = 1; index < toc.entries; ++index) {
        const std::span<const u8> entry = Entry(toc, index);
        const std::string digest = Hex(entry.first(DigestBytes));
        const auto good = expected.find(digest);
        if (good == expected.end()) {
            continue;
        }
        good->second.found = true;
        const u64 size = BigEndian(entry.subspan(EntrySize, 5));
        if (size == good->second.size) {
            continue;
        }
        ++difference.resized;
        // A name counts when it is the entry's: a damaged list gives another.
        const bool named = index - 1 < names.size() && NameDigest(names[index - 1]) == digest;
        const std::string name = named ? names[index - 1] : "the file of name MD5 " + digest;
        if (named && difference.names.size() < NamedFiles) {
            difference.names.push_back(name);
        }
        if (logged()) {
            Log(true, "{} is {} bytes, {} in a good dump", name, size, good->second.size);
        }
    }
    for (const auto& [digest, good] : expected) {
        if (good.found) {
            continue;
        }
        ++difference.missing;
        if (logged()) {
            Log(true, "the file of name MD5 {} is missing, {} bytes in a good dump", digest,
                good.size);
        }
    }
    return difference;
}

// Adds what a comparison found to the player's list: the archive with each of the first files it
// names, or alone where it names none, and the count of the rest.
void Report(std::vector<std::string>& differing, std::string_view archive,
            const Difference& difference) {
    if (difference.names.empty()) {
        differing.emplace_back(archive);
        return;
    }
    for (const std::string& name : difference.names) {
        differing.push_back(fmt::format("{}: {}", archive, name));
    }
    const u64 count = difference.missing + difference.resized;
    if (count > difference.names.size()) {
        differing.push_back(fmt::format("(+{})", count - difference.names.size()));
    }
}

// The SHA-256 of eboot.bin's loaded segments, each after its address, as the loader reads them
// from a plain or a signed file alike; its size too.
std::optional<std::string> SegmentsHash(IBackend& files, u64& size) {
    std::unique_ptr<IFile> file = files.Open("eboot.bin", Common::FS::FileAccessMode::Read);
    if (file == nullptr) {
        return std::nullopt;
    }
    size = file->Size();
    Core::Loader::Elf elf;
    elf.Open(std::move(file));
    if (!elf.IsElfFile()) {
        return std::nullopt;
    }
    Hash hash{EVP_sha256()};
    for (const elf_program_header& header : elf.GetProgramHeader()) {
        if (header.p_type != PT_LOAD && header.p_type != PT_SCE_RELRO) {
            continue;
        }
        std::vector<u8> bytes(header.p_filesz);
        if (!elf.TryLoadSegment(reinterpret_cast<u64>(bytes.data()), header.p_offset,
                                bytes.size())) {
            return std::nullopt;
        }
        hash.Add({reinterpret_cast<const u8*>(&header.p_vaddr), sizeof(header.p_vaddr)});
        hash.Add(bytes);
    }
    return hash.Finish();
}

// Logs the eboot's size and hash. Where the dump is compared, the result is logged too, and an
// eboot that differs from the reference of a listed title counts as differing.
void CompareEboot(const std::string& title, IBackend& files, bool compared,
                  std::vector<std::string>& differing) {
    u64 size = 0;
    const std::optional<std::string> hash = SegmentsHash(files, size);
    if (!hash) {
        Log(true, "eboot.bin, {} bytes, cannot be read as a program", size);
        return;
    }
    if (!compared) {
        Log(false,
            "eboot.bin of {}, {} bytes, segments SHA-256 {}; loose "
            "files are not compared",
            title, size, *hash);
        return;
    }
    const std::vector<Form>& eboots = TheReference().eboots;
    const bool known = std::ranges::any_of(eboots, [&](const Form& f) { return f.hash == *hash; });
    const bool listed = FormOf(eboots, title).has_value();
    Log(false, "eboot.bin of {}, {} bytes, segments SHA-256 {}: {}", title, size, *hash,
        known    ? "as in a good dump"
        : listed ? "NOT as in a good dump"
                 : "no reference for this title");
    if (!known && listed) {
        differing.emplace_back("eboot.bin");
    }
}

// An archive as a .psarc file: a known size and table of contents make it good. Another one of a
// listed title is compared file by file.
void CompareArchive(const std::string& title, const Archive& archive, IBackend& root,
                    std::vector<std::string>& differing) {
    const std::string name = std::string{archive.name} + ".psarc";
    const std::unique_ptr<IFile> file = root.Open(name, Common::FS::FileAccessMode::Read);
    if (file == nullptr) {
        return;
    }
    const u64 size = file->Size();
    const std::optional<size_t> form = FormOf(archive.forms, title);
    const std::optional<Toc> toc = ReadToc(*file);
    if (!toc) {
        Log(true, "{}, {} bytes, has no table of contents to read", name, size);
        differing.push_back(name);
        return;
    }
    Hash sha{EVP_sha256()};
    sha.Add(toc->bytes);
    const std::string hash = sha.Finish();
    const bool known = std::ranges::any_of(
        archive.forms, [&](const Form& f) { return f.size == size && f.hash == hash; });
    Log(false, "{}, {} bytes, {} entries, table of contents SHA-256 {}: {}", name, size,
        toc->entries, hash,
        known  ? "as in a good dump"
        : form ? "NOT as in a good dump"
               : "no reference for this title");
    if (known || !form) {
        return;
    }
    const Form& good = archive.forms[*form];
    if (size != good.size) {
        Log(true, "{} is {} bytes, {} in a good dump", name, size, good.size);
    }
    const Difference difference = Compare(*file, *toc, archive, *form);
    Log(true, "{} holds {} files; of a good dump's files, {} missing and {} of another size", name,
        toc->entries - 1, difference.missing, difference.resized);
    Report(differing, name, difference);
}

} // namespace

void LogDumpAgain() {
    std::ranges::for_each(g_lines, Write);
    g_lines.clear();
}

WholeRead::WholeRead(std::string title_id, std::vector<std::filesystem::path> roots) {
    for (const Archive& archive : TheReference().archives) {
        archives.push_back({.name = std::string{archive.name} + ".psarc"});
    }
    thread = std::jthread{[this, title_id = std::move(title_id), roots = std::move(roots)](
                              std::stop_token stop) { Run(stop, title_id, roots); }};
}

std::vector<WholeArchive> WholeRead::Archives() const {
    std::scoped_lock lock{mutex};
    return archives;
}

bool WholeRead::Done() const {
    std::scoped_lock lock{mutex};
    return std::ranges::none_of(archives, [](const WholeArchive& archive) {
        return archive.state == WholeArchive::State::Waiting ||
               archive.state == WholeArchive::State::Reading;
    });
}

void WholeRead::Run(const std::stop_token& stop, const std::string& title_id,
                    const std::vector<std::filesystem::path>& roots) {
    using State = WholeArchive::State;
    std::vector<std::unique_ptr<IBackend>> opened;
    for (const std::filesystem::path& root : roots) {
        if (std::unique_ptr<IBackend> files = Core::FileSys::OpenGameBackend(root)) {
            opened.push_back(std::move(files));
        }
    }
    const std::vector<Archive>& known = TheReference().archives;
    std::vector<u8> bytes(WholeReadBytes);
    for (size_t index = 0; index < known.size(); ++index) {
        const std::string name = std::string{known[index].name} + ".psarc";
        const auto set = [&](u64 size, u64 read, State state) {
            std::scoped_lock lock{mutex};
            archives[index].size = size;
            archives[index].read = read;
            archives[index].state = state;
        };
        // The first root that holds the archive has the one the game reads.
        std::unique_ptr<IFile> file;
        for (const std::unique_ptr<IBackend>& files : opened) {
            if (files->Exists(name)) {
                file = files->Open(name, Common::FS::FileAccessMode::Read);
                break;
            }
        }
        if (file == nullptr) {
            Log(true, "{} cannot be opened", name);
            set(0, 0, State::Unread);
            continue;
        }
        const u64 size = file->Size();
        Hash sha{EVP_sha256()};
        u64 read = 0;
        while (read < size) {
            set(size, read, State::Reading);
            if (stop.stop_requested()) {
                return;
            }
            const s64 got = file->Read(bytes.data(), std::min<u64>(bytes.size(), size - read));
            if (got <= 0) {
                break;
            }
            sha.Add(std::span{bytes}.first(static_cast<size_t>(got)));
            read += static_cast<u64>(got);
        }
        if (read != size) {
            Log(true, "{}, {} bytes, cannot be read after byte {}", name, size, read);
            set(size, read, State::Unread);
            continue;
        }
        const std::string hash = sha.Finish();
        const std::vector<Form>& forms = known[index].forms;
        const State state =
            std::ranges::any_of(forms, [&](const Form& form) { return form.whole == hash; })
                ? State::Good
            : FormOf(forms, title_id) ? State::Differs
                                      : State::NoReference;
        Log(state == State::Differs, "{}, {} bytes, whole file SHA-256 {}: {}", name, size, hash,
            state == State::Good      ? "as in a good dump"
            : state == State::Differs ? "NOT as in a good dump"
                                      : "no reference for this title");
        set(size, read, state);
    }
}

std::vector<std::string> CompareDump(const std::string& title_id, IBackend& game, IBackend* update,
                                     IBackend& eboot, bool archives) {
    std::vector<std::string> differing;
    CompareEboot(title_id, eboot, archives, differing);
    if (!archives) {
        return differing;
    }
    for (const Archive& archive : TheReference().archives) {
        // The update's root is read before the game's.
        const std::string name = std::string{archive.name} + ".psarc";
        IBackend& root = update != nullptr && update->Exists(name) ? *update : game;
        CompareArchive(title_id, archive, root, differing);
    }
    return differing;
}

} // namespace Gr2Port::GameCheck
