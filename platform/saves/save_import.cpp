// PS2 saves into the native save folder (save_import.h). The memory card's file system as the public descriptions
// of it give it (the PS2 memory card file system as documented by the homebrew community: superblock, indirect FAT,
// 512-byte directory entries); written for this project.
#include "save_import.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <system_error>

namespace gt4::saves {

namespace fs = std::filesystem;

namespace {

constexpr char kProduct[] = "BASCUS-97328";   // GT4 NTSC-U's saves are named after it

bool ours(const std::string& name) { return name.rfind(kProduct, 0) == 0; }

// A path as UTF-8 text (C++20's u8string is a string of char8_t).
std::string text(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

uint16_t u16(const unsigned char* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t u32(const unsigned char* p) { return static_cast<uint32_t>(p[0] | p[1] << 8 | p[2] << 16) | static_cast<uint32_t>(p[3]) << 24; }

std::string entryName(const unsigned char* entry) {
    const char* n = reinterpret_cast<const char*>(entry + 0x40);
    return std::string(n, strnlen(n, 32));
}

// A directory entry's mode: it exists, and is a directory or a file.
constexpr uint16_t kExists = 0x8000, kDirectory = 0x0020, kFile = 0x0010;

bool readFile(const fs::path& p, std::vector<unsigned char>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// A memory card image: pages of `pageSize` bytes, each followed by `spare` bytes of ECC (0 in a dump without).
class Card {
public:
    bool open(std::vector<unsigned char> image, std::string& error) {
        data = std::move(image);
        if (data.size() < 0x154 || std::memcmp(data.data(), "Sony PS2 Memory Card Format ", 28) != 0) {
            const bool blank = std::all_of(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(data.size(), 0x2000)),
                                           [](unsigned char b) { return b == 0xFF; });
            error = blank && !data.empty() ? "an unformatted memory card (no saves)" : "not a PS2 memory card image (no superblock)";
            return false;
        }
        const unsigned char* s = data.data();
        pageSize = u16(s + 0x28);
        pagesPerCluster = u16(s + 0x2A);
        allocOffset = u32(s + 0x34);
        rootCluster = u32(s + 0x3C);
        for (int k = 0; k < 32; ++k) indirect[k] = u32(s + 0x50 + 4 * k);
        if (pageSize != 512 || pagesPerCluster == 0 || pagesPerCluster > 16) {
            error = "an unusual memory card layout (page size " + std::to_string(pageSize) + ")";
            return false;
        }
        const size_t clusters = u32(s + 0x30);
        // with ECC, 16 bytes after each page (PCSX2's .ps2: 8,650,752 bytes for 8 MiB)
        spare = data.size() >= clusters * pagesPerCluster * (pageSize + 16) ? 16 : 0;
        clusterSize = static_cast<size_t>(pageSize) * pagesPerCluster;
        return true;
    }

    // The saves on the card: its root directory's folders (only GT4's, when `onlyOurs`).
    std::vector<SaveEntry> saves(bool onlyOurs, std::string& error) {
        std::vector<SaveEntry> out;
        std::vector<unsigned char> root, first;
        if (!readChain(rootCluster, 512, first, error)) return out;
        if (!readDirectory(rootCluster, u32(first.data() + 4), root, error)) return out;
        for (size_t at = 0; at + 512 <= root.size(); at += 512) {
            const unsigned char* e = root.data() + at;
            const uint16_t mode = u16(e);
            const std::string name = entryName(e);
            if (!(mode & kExists) || !(mode & kDirectory) || name == "." || name == ".." || (onlyOurs && !ours(name))) continue;
            SaveEntry save;
            save.folder = name;
            std::vector<unsigned char> dir;
            if (!readDirectory(u32(e + 0x10), u32(e + 4), dir, error)) return {};
            for (size_t k = 0; k + 512 <= dir.size(); k += 512) {
                const unsigned char* f = dir.data() + k;
                const uint16_t fm = u16(f);
                const std::string fname = entryName(f);
                if (!(fm & kExists) || !(fm & kFile)) continue;
                std::vector<unsigned char> bytes;
                if (!readChain(u32(f + 0x10), u32(f + 4), bytes, error)) return {};
                save.files.emplace_back(fname, std::move(bytes));
            }
            out.push_back(std::move(save));
        }
        return out;
    }

private:
    std::vector<unsigned char> data;
    uint32_t pageSize = 512, pagesPerCluster = 2, allocOffset = 0, rootCluster = 0, spare = 0;
    uint32_t indirect[32] = {};
    size_t clusterSize = 1024;

    // A cluster by its number on the card (not counted from the allocation offset).
    bool cluster(uint32_t number, unsigned char* out) const {
        for (uint32_t p = 0; p < pagesPerCluster; ++p) {
            const size_t page = static_cast<size_t>(number) * pagesPerCluster + p;
            const size_t at = page * (pageSize + spare);
            if (at + pageSize > data.size()) return false;
            std::memcpy(out + p * pageSize, data.data() + at, pageSize);
        }
        return true;
    }

    // The FAT entry of an allocatable cluster (counted from the allocation offset): through the indirect FAT
    // clusters, each a list of FAT clusters, each a list of entries.
    bool fat(uint32_t n, uint32_t& value) const {
        const uint32_t perCluster = static_cast<uint32_t>(clusterSize / 4);
        const uint32_t fatIndex = n / perCluster, indirectIndex = fatIndex / perCluster;
        if (indirectIndex >= 32) return false;
        std::vector<unsigned char> buf(clusterSize);
        if (!cluster(indirect[indirectIndex], buf.data())) return false;
        const uint32_t fatCluster = u32(buf.data() + 4 * (fatIndex % perCluster));
        if (!cluster(fatCluster, buf.data())) return false;
        value = u32(buf.data() + 4 * (n % perCluster));
        return true;
    }

    // `bytes` from the chain starting at allocatable cluster `start`.
    bool readChain(uint32_t start, size_t bytes, std::vector<unsigned char>& out, std::string& error) const {
        out.clear();
        std::vector<unsigned char> buf(clusterSize);
        uint32_t n = start;
        for (size_t guard = 0; out.size() < bytes; ++guard) {
            if (guard > data.size() / clusterSize || !cluster(n + allocOffset, buf.data())) {
                error = "a broken chain of clusters on the card";
                return false;
            }
            out.insert(out.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(std::min(clusterSize, bytes - out.size())));
            if (out.size() >= bytes) break;
            uint32_t next = 0;
            if (!fat(n, next) || !(next & 0x80000000u) || (next & 0x7FFFFFFFu) == 0x7FFFFFFFu) {
                error = "a file shorter than its directory entry says";
                return false;
            }
            n = next & 0x7FFFFFFFu;
        }
        return true;
    }

    // A directory's `count` entries (512 bytes each). The count is the length in the directory's entry in its
    // parent (the root's in its own "." entry); a folder's "." holds 0 on real cards.
    bool readDirectory(uint32_t start, uint32_t count, std::vector<unsigned char>& out, std::string& error) const {
        if (count == 0 || count > 4096) {
            error = "a damaged directory on the card";
            return false;
        }
        return readChain(start, static_cast<size_t>(count) * 512, out, error);
    }
};

// A .psu export: 512-byte entries as on the card (the folder, ".", "..", then each file followed by its data in
// 1024-byte units).
std::vector<SaveEntry> readPsu(const std::vector<unsigned char>& d, std::string& error) {
    std::vector<SaveEntry> out;
    if (d.size() < 512 * 3 || !(u16(d.data()) & kDirectory)) {
        error = "not a .psu save";
        return out;
    }
    SaveEntry save;
    save.folder = entryName(d.data());
    const uint32_t count = u32(d.data() + 4);
    size_t at = 512 * 3;   // the folder, ".", ".."
    for (uint32_t k = 2; k < count && at + 512 <= d.size(); ++k) {
        const unsigned char* e = d.data() + at;
        const uint32_t size = u32(e + 4);
        at += 512;
        if (!(u16(e) & kFile)) continue;
        if (at + size > d.size()) {
            error = "a .psu shorter than its entries say";
            return {};
        }
        save.files.emplace_back(entryName(e), std::vector<unsigned char>(d.begin() + static_cast<std::ptrdiff_t>(at),
                                                                         d.begin() + static_cast<std::ptrdiff_t>(at + size)));
        at += (size + 1023) / 1024 * 1024;
    }
    out.push_back(std::move(save));
    return out;
}

// A PCSX2 folder memory card (or any folder of save folders).
std::vector<SaveEntry> readFolder(const fs::path& dir, std::string& error) {
    std::vector<SaveEntry> out;
    std::error_code ec;
    for (const auto& sub : fs::directory_iterator(dir, ec)) {
        if (!sub.is_directory(ec)) continue;
        const std::string name = text(sub.path().filename());
        if (!ours(name)) continue;
        SaveEntry save;
        save.folder = name;
        for (const auto& f : fs::directory_iterator(sub.path(), ec)) {
            const std::string fname = text(f.path().filename());
            if (!f.is_regular_file(ec) || fname.rfind("_pcsx2", 0) == 0) continue;   // PCSX2's own index files
            std::vector<unsigned char> bytes;
            if (!readFile(f.path(), bytes)) {
                error = "cannot read " + text(f.path());
                return {};
            }
            save.files.emplace_back(fname, std::move(bytes));
        }
        out.push_back(std::move(save));
    }
    if (ec) error = ec.message();
    return out;
}

std::string stamp() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y%m%d-%H%M%S", &tm);
    return buf;
}

// .backup/imports/DATE in the save folder, one not used yet
fs::path displacedFolder(const fs::path& saveFolder) {
    const std::string when = stamp();
    const fs::path base = saveFolder / ".backup" / "imports";
    fs::path dir = base / when;
    std::error_code ec;
    for (int n = 2; fs::exists(dir, ec); ++n) {
        char suffix[16];
        std::snprintf(suffix, sizeof suffix, "-%02d", n);
        dir = base / (when + suffix);
    }
    return dir;
}

void put16(std::vector<unsigned char>& d, size_t at, uint16_t v) {
    d[at] = static_cast<unsigned char>(v);
    d[at + 1] = static_cast<unsigned char>(v >> 8);
}
void put32(std::vector<unsigned char>& d, size_t at, uint32_t v) {
    for (int k = 0; k < 4; ++k) d[at + k] = static_cast<unsigned char>(v >> (8 * k));
}

// A 512-byte card directory entry: mode, length, creation and modification time (the PS2's clock, JST), name.
void putEntry(std::vector<unsigned char>& out, uint16_t mode, uint32_t length, const std::tm& jst, const std::string& name) {
    const size_t at = out.size();
    out.resize(at + 512, 0);
    put16(out, at, mode);
    put32(out, at + 4, length);
    for (size_t t : {size_t(0x08), size_t(0x18)}) {
        out[at + t + 1] = static_cast<unsigned char>(jst.tm_sec);
        out[at + t + 2] = static_cast<unsigned char>(jst.tm_min);
        out[at + t + 3] = static_cast<unsigned char>(jst.tm_hour);
        out[at + t + 4] = static_cast<unsigned char>(jst.tm_mday);
        out[at + t + 5] = static_cast<unsigned char>(jst.tm_mon + 1);
        put16(out, at + t + 6, static_cast<uint16_t>(jst.tm_year + 1900));
    }
    std::memcpy(out.data() + at + 0x40, name.data(), std::min<size_t>(name.size(), 31));
}

}  // namespace

fs::path defaultSaveFolder() {
#ifdef _WIN32
    if (const char* local = std::getenv("LOCALAPPDATA")) return fs::u8path(local) / "gt4-native" / "saves";
#else
    if (const char* data = std::getenv("XDG_DATA_HOME")) return fs::u8path(data) / "gt4-native" / "saves";
    if (const char* home = std::getenv("HOME")) return fs::u8path(home) / ".local" / "share" / "gt4-native" / "saves";
#endif
    return fs::path("saves");
}

std::vector<SaveEntry> readSaves(const fs::path& source, std::string& error) {
    std::error_code ec;
    if (fs::is_directory(source, ec)) return readFolder(source, error);
    std::vector<unsigned char> bytes;
    if (!readFile(source, bytes)) {
        error = "cannot read " + text(source);
        return {};
    }
    std::string ext = text(source.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".psu") return readPsu(bytes, error);
    Card card;
    if (!card.open(std::move(bytes), error)) return {};
    return card.saves(true, error);
}

ImportResult importSave(const fs::path& source, const fs::path& saveFolder) {
    ImportResult r;
    std::string error;
    std::vector<SaveEntry> saves = readSaves(source, error);
    if (!error.empty()) {
        r.message = error;
        return r;
    }
    if (saves.empty()) {
        r.message = "no Gran Turismo 4 save (" + std::string(kProduct) + "...) in " + text(source);
        return r;
    }
    std::error_code ec;
    fs::create_directories(saveFolder, ec);
    // a save the import replaces goes to .backup/imports/DATE/ (the game does not see names starting with ".")
    const fs::path displaced = displacedFolder(saveFolder);
    for (const SaveEntry& save : saves) {
        const fs::path dir = saveFolder / fs::u8path(save.folder);
        if (fs::exists(dir, ec)) {
            fs::create_directories(displaced, ec);
            if (!ec) fs::rename(dir, displaced / fs::u8path(save.folder), ec);
        }
        if (ec || !fs::create_directories(dir, ec)) {
            r.message = "cannot write " + text(dir) + (ec ? ": " + ec.message() : "");
            return r;
        }
        for (const auto& [name, bytes] : save.files) {
            std::ofstream f(dir / fs::u8path(name), std::ios::binary);
            f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!f) {
                r.message = "cannot write " + text(dir / fs::u8path(name));
                return r;
            }
        }
        r.imported.push_back(save.folder);
    }
    r.ok = true;
    r.message = "imported " + std::to_string(r.imported.size()) + " save(s) into " + text(saveFolder);
    return r;
}

namespace {

// One native save folder's files, the game's own only (no "." names), sorted; empty with `error` set when it can't be
// read or a name is too long for a card (31 bytes).
std::vector<std::pair<std::string, std::vector<unsigned char>>> saveFiles(const fs::path& dir, std::string& error) {
    std::vector<std::pair<std::string, std::vector<unsigned char>>> files;
    std::error_code ec;
    for (const auto& f : fs::directory_iterator(dir, ec)) {
        const std::string name = text(f.path().filename());
        if (!f.is_regular_file(ec) || name.empty() || name[0] == '.') continue;   // the game's own files only
        if (name.size() > 31) {
            error = "a file name too long for a card: " + name;
            return {};
        }
        std::vector<unsigned char> bytes;
        if (!readFile(f.path(), bytes)) {
            error = "cannot read " + text(f.path());
            return {};
        }
        files.emplace_back(name, std::move(bytes));
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return files;
}

// Now on the PS2's clock, which keeps JST.
std::tm nowJst() {
    const std::time_t now = std::time(nullptr) + 9 * 3600;
    std::tm jst{};
#ifdef _WIN32
    gmtime_s(&jst, &now);
#else
    gmtime_r(&now, &jst);
#endif
    return jst;
}

// `bytes` written to `out` whole or not at all: to a new file beside it, then renamed to `out`. An existing `out`
// is never opened; it is replaced (by the rename) only with `overwrite`, else the export is refused.
bool writeWhole(const fs::path& out, const std::vector<unsigned char>& bytes, bool overwrite) {
    std::error_code ec;
    if (!overwrite && fs::exists(fs::symlink_status(out, ec))) return false;
    const fs::path temp = out.parent_path() / fs::u8path("." + text(out.filename()) + ".tmp");
    {
        std::ofstream f(temp, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        f.close();
        if (!f) {
            fs::remove(temp, ec);
            return false;
        }
    }
    if (!overwrite && fs::exists(fs::symlink_status(out, ec))) {   // (one appeared meanwhile)
        fs::remove(temp, ec);
        return false;
    }
    std::error_code renamed;   // (its own: removing the temporary file would reset `ec` to success)
    fs::rename(temp, out, renamed);
    if (renamed) {   // such as an output held open by another program (PCSX2 with the card mounted)
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

}  // namespace

// the refusal of an existing output (exportSave, exportCard)
static bool refuseExisting(const fs::path& out, bool force, ExportResult& r) {
    std::error_code ec;
    if (force || !fs::exists(fs::symlink_status(out, ec))) return false;
    r.message = text(out) + " exists: not replaced (--force replaces it with a new file; it is never opened)";
    return true;
}

ExportResult exportSave(const fs::path& saveFolder, const std::string& save, const fs::path& out, bool force) {
    ExportResult r;
    if (refuseExisting(out, force, r)) return r;
    const fs::path dir = saveFolder / fs::u8path(save);
    std::error_code ec;
    if (save.empty() || save[0] == '.' || save.find_first_of("/\\") != std::string::npos || !fs::is_directory(dir, ec)) {
        r.message = "no save " + save + " in " + text(saveFolder);
        return r;
    }
    std::string error;
    const auto files = saveFiles(dir, error);
    if (!error.empty()) {
        r.message = error;
        return r;
    }
    const std::tm jst = nowJst();
    // the folder (its length: the entries in it, "." and ".." included), ".", "..", then each file and its data
    // in 1024-byte units, as uLaunchELF and PCSX2's memory card editor write it
    std::vector<unsigned char> psu;
    putEntry(psu, 0x8427, static_cast<uint32_t>(files.size() + 2), jst, save);
    putEntry(psu, 0x8427, 0, jst, ".");
    putEntry(psu, 0x8427, 0, jst, "..");
    for (const auto& [name, bytes] : files) {
        putEntry(psu, 0x8497, static_cast<uint32_t>(bytes.size()), jst, name);
        psu.insert(psu.end(), bytes.begin(), bytes.end());
        psu.resize(psu.size() + (1024 - bytes.size() % 1024) % 1024, 0);
    }
    if (!writeWhole(out, psu, force)) {
        r.message = "cannot write " + text(out);
        return r;
    }
    r.ok = true;
    r.message = "exported " + save + " (" + std::to_string(files.size()) + " files) to " + text(out);
    return r;
}

namespace {

// The PS2 memory card's ECC: three bytes for each 128-byte quarter of a page (a column parity and two line parities,
// the Hamming code the card's controller keeps), as the public descriptions of the card give it.
void ecc128(const unsigned char* d, unsigned char out[3]) {
    static const unsigned char masks[7] = {0x55, 0x33, 0x0F, 0x00, 0xAA, 0xCC, 0xF0};
    unsigned char column = 0x77, line0 = 0x7F, line1 = 0x7F;
    for (int i = 0; i < 128; ++i) {
        const unsigned char b = d[i];
        unsigned char c = 0;
        for (int k = 0; k < 7; ++k) c |= static_cast<unsigned char>((std::popcount(static_cast<unsigned>(b & masks[k])) & 1) << k);
        column ^= c;
        if (std::popcount(static_cast<unsigned>(b)) & 1) {
            line0 ^= static_cast<unsigned char>(~i);
            line1 ^= static_cast<unsigned char>(i);
        }
    }
    out[0] = column;
    out[1] = line0 & 0x7F;
    out[2] = line1 & 0x7F;
}

}  // namespace

ExportResult exportCard(const fs::path& saveFolder, const fs::path& out, bool force) {
    ExportResult r;
    if (refuseExisting(out, force, r)) return r;
    // GT4's saves in the folder (BASCUS-97328...), each with its files
    std::vector<SaveEntry> saves;
    std::error_code ec;
    for (const auto& sub : fs::directory_iterator(saveFolder, ec)) {
        const std::string name = text(sub.path().filename());
        if (!sub.is_directory(ec) || !ours(name)) continue;
        std::string error;
        SaveEntry s;
        s.folder = name;
        s.files = saveFiles(sub.path(), error);
        if (!error.empty()) {
            r.message = error;
            return r;
        }
        saves.push_back(std::move(s));
    }
    if (saves.empty()) {
        r.message = "no Gran Turismo 4 save (" + std::string(kProduct) + "...) in " + text(saveFolder);
        return r;
    }
    std::sort(saves.begin(), saves.end(), [](const SaveEntry& a, const SaveEntry& b) { return a.folder < b.folder; });

    // An 8 MiB card in the standard layout (what the PS2 and PCSX2 format): 512-byte pages, two to a cluster, 8192
    // clusters; the superblock in cluster 0, the indirect FAT cluster at 8, the FAT in 9-40, files from 41 (cluster
    // numbers in the FAT and the directories count from there), 8135 of them; the last two erase blocks are the
    // backups. Pages not used are written as zeros (nothing reads them: the FAT says which clusters are free), except
    // backup block 1022, which must be erased (0xFF, its spare bytes too) as mymcplus formats it: the card's server takes
    // a backup block that is not erased for an unfinished write and copies it back at mount (lane U's run: zeros over
    // the superblock, the card lost).
    constexpr uint32_t kPage = 512, kPerCluster = 2, kCluster = kPage * kPerCluster, kClusters = 8192;
    constexpr uint32_t kIndirect = 8, kFatFirst = 9, kFatClusters = 32, kAllocOffset = 41, kAllocEnd = 8135;
    std::vector<unsigned char> card(static_cast<size_t>(kClusters) * kCluster, 0);
    const auto at = [&](uint32_t cluster) { return static_cast<size_t>(cluster) * kCluster; };
    // the FAT: free (0x7FFFFFFF) for every allocatable cluster, then chains as files are laid down
    std::vector<uint32_t> fat(kFatClusters * (kCluster / 4), 0xFFFFFFFFu);
    for (uint32_t k = 0; k < kAllocEnd; ++k) fat[k] = 0x7FFFFFFFu;
    uint32_t next = 0;   // the next free allocatable cluster
    const auto chain = [&](size_t bytes) -> int64_t {   // the first cluster of a chain for `bytes`, or -1: full
        const uint32_t n = static_cast<uint32_t>(std::max<size_t>(1, (bytes + kCluster - 1) / kCluster));
        if (next + n > kAllocEnd) return -1;
        const uint32_t first = next;
        for (uint32_t k = 0; k < n; ++k) fat[first + k] = k + 1 < n ? 0x80000000u | (first + k + 1) : 0xFFFFFFFFu;
        next += n;
        return first;
    };
    const auto writeChain = [&](uint32_t first, const std::vector<unsigned char>& bytes) {
        std::memcpy(card.data() + at(kAllocOffset + first), bytes.data(), bytes.size());   // (a chain is contiguous here)
    };
    const std::tm jst = nowJst();
    // the root: ".", "..", then each save's folder; each folder: ".", "..", then its files
    std::vector<unsigned char> root;
    const int64_t rootCluster = chain(static_cast<size_t>(saves.size() + 2) * 512);
    std::vector<std::vector<unsigned char>> dirs;
    std::vector<int64_t> dirClusters;
    for (const SaveEntry& s : saves) dirClusters.push_back(chain((s.files.size() + 2) * 512));
    putEntry(root, 0x8427, static_cast<uint32_t>(saves.size() + 2), jst, ".");
    putEntry(root, 0xA426, 0, jst, "..");
    for (size_t i = 0; i < saves.size(); ++i) {
        const SaveEntry& s = saves[i];
        std::vector<unsigned char> dir;
        // "." points back at where the folder sits in its parent: the parent's first cluster (the root's) and the
        // folder's entry there. The card's server follows it to update the folder's entry (length, time) when a
        // file in it is written, so it must not point at the folder itself (lane U's mymcplus check).
        putEntry(dir, 0x8427, 0, jst, ".");
        put32(dir, 0x10, static_cast<uint32_t>(rootCluster));
        put32(dir, 0x14, static_cast<uint32_t>(i + 2));   // its entry in the root
        putEntry(dir, 0x8427, 0, jst, "..");
        for (const auto& [name, bytes] : s.files) {
            const int64_t c = chain(bytes.size());
            if (c < 0) {
                r.message = "the saves do not fit on an 8 MiB card";
                return r;
            }
            putEntry(dir, 0x8497, static_cast<uint32_t>(bytes.size()), jst, name);
            put32(dir, dir.size() - 512 + 0x10, static_cast<uint32_t>(c));
            writeChain(static_cast<uint32_t>(c), bytes);
        }
        if (dirClusters[i] < 0) {
            r.message = "the saves do not fit on an 8 MiB card";
            return r;
        }
        writeChain(static_cast<uint32_t>(dirClusters[i]), dir);
        putEntry(root, 0x8427, static_cast<uint32_t>(s.files.size() + 2), jst, s.folder);
        put32(root, root.size() - 512 + 0x10, static_cast<uint32_t>(dirClusters[i]));
    }
    writeChain(static_cast<uint32_t>(rootCluster), root);
    // the FAT clusters and the indirect cluster listing them
    for (uint32_t k = 0; k < kFatClusters; ++k)
        for (uint32_t e = 0; e < kCluster / 4; ++e) {
            std::vector<unsigned char> word(4);
            put32(word, 0, fat[k * (kCluster / 4) + e]);
            std::memcpy(card.data() + at(kFatFirst + k) + 4 * e, word.data(), 4);
        }
    {
        std::vector<unsigned char> ind(kCluster, 0);
        for (uint32_t k = 0; k < kFatClusters; ++k) put32(ind, 4 * k, kFatFirst + k);
        std::memcpy(card.data() + at(kIndirect), ind.data(), kCluster);
    }
    // the superblock
    {
        std::vector<unsigned char> s(0x154, 0);
        std::memcpy(s.data(), "Sony PS2 Memory Card Format 1.2.0.0", 35);
        put16(s, 0x28, kPage);
        put16(s, 0x2A, kPerCluster);
        put16(s, 0x2C, 16);       // pages per erase block
        put16(s, 0x2E, 0xFF00);
        put32(s, 0x30, kClusters);
        put32(s, 0x34, kAllocOffset);
        put32(s, 0x38, kAllocEnd);
        put32(s, 0x3C, static_cast<uint32_t>(rootCluster));
        put32(s, 0x40, 1023);     // the backup erase blocks
        put32(s, 0x44, 1022);
        put32(s, 0x50, kIndirect);   // the indirect FAT clusters: one
        for (uint32_t k = 1; k < 32; ++k) put32(s, 0x50 + 4 * k, 0);
        for (uint32_t k = 0; k < 32; ++k) put32(s, 0xD0 + 4 * k, 0xFFFFFFFFu);   // no bad erase block
        s[0x150] = 2;      // a PS2 card
        // the card's flags as mymcplus formats a card (0x01 the card server keeps ECC, 0x08 a bad-block list): PCSX2 then
        // rewrites a page with its ECC. With 0x52 (no 0x01) a page PCSX2 wrote after a save had no valid ECC (lane U's run)
        s[0x151] = 0x2B;
        std::memcpy(card.data(), s.data(), s.size());
    }
    // PCSX2's .ps2: each 512-byte page followed by its 16 bytes of ECC (12, then 4 zero bytes); erase block 1022 all 0xFF
    constexpr size_t kErasedFirst = 1022u * 16 * kPage, kErasedEnd = 1023u * 16 * kPage;
    std::vector<unsigned char> image;
    image.reserve(card.size() / kPage * (kPage + 16));
    for (size_t p = 0; p < card.size(); p += kPage) {
        if (p >= kErasedFirst && p < kErasedEnd) {
            image.insert(image.end(), kPage + 16, 0xFF);
            continue;
        }
        image.insert(image.end(), card.begin() + static_cast<std::ptrdiff_t>(p), card.begin() + static_cast<std::ptrdiff_t>(p + kPage));
        unsigned char spare[16] = {};
        for (int q = 0; q < 4; ++q) ecc128(card.data() + p + 128 * q, spare + 3 * q);
        image.insert(image.end(), spare, spare + 16);
    }
    if (!writeWhole(out, image, force)) {
        r.message = "cannot write " + text(out);
        return r;
    }
    r.ok = true;
    size_t files = 0;
    for (const SaveEntry& s : saves) files += s.files.size();
    r.message = "wrote an 8 MiB PCSX2 memory card with " + std::to_string(saves.size()) + " save(s) (" +
                std::to_string(files) + " files) to " + text(out);
    return r;
}
}  // namespace gt4::saves
