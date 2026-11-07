// PS2 saves into the native save folder (save_import.h). The memory card's file system as the public descriptions
// of it give it (the PS2 memory card file system as documented by the homebrew community: superblock, indirect FAT,
// 512-byte directory entries); written for this project.
#include "save_import.h"

#include <algorithm>
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

ExportResult exportSave(const fs::path& saveFolder, const std::string& save, const fs::path& out) {
    ExportResult r;
    const fs::path dir = saveFolder / fs::u8path(save);
    std::error_code ec;
    if (save.empty() || save[0] == '.' || save.find_first_of("/\\") != std::string::npos || !fs::is_directory(dir, ec)) {
        r.message = "no save " + save + " in " + text(saveFolder);
        return r;
    }
    std::vector<std::pair<std::string, std::vector<unsigned char>>> files;
    for (const auto& f : fs::directory_iterator(dir, ec)) {
        const std::string name = text(f.path().filename());
        if (!f.is_regular_file(ec) || name.empty() || name[0] == '.') continue;   // the game's own files only
        if (name.size() > 31) {
            r.message = "a file name too long for a card: " + name;
            return r;
        }
        std::vector<unsigned char> bytes;
        if (!readFile(f.path(), bytes)) {
            r.message = "cannot read " + text(f.path());
            return r;
        }
        files.emplace_back(name, std::move(bytes));
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    const std::time_t now = std::time(nullptr) + 9 * 3600;   // the PS2's clock keeps JST
    std::tm jst{};
#ifdef _WIN32
    gmtime_s(&jst, &now);
#else
    gmtime_r(&now, &jst);
#endif
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
    // whole or not at all: written beside, then renamed over
    const fs::path temp = out.parent_path() / fs::u8path("." + text(out.filename()) + ".tmp");
    {
        std::ofstream f(temp, std::ios::binary);
        f.write(reinterpret_cast<const char*>(psu.data()), static_cast<std::streamsize>(psu.size()));
        f.close();
        if (!f) {
            fs::remove(temp, ec);
            r.message = "cannot write " + text(out);
            return r;
        }
    }
    fs::rename(temp, out, ec);
    if (ec) {
        fs::remove(temp, ec);
        r.message = "cannot write " + text(out);
        return r;
    }
    r.ok = true;
    r.message = "exported " + save + " (" + std::to_string(files.size()) + " files) to " + text(out);
    return r;
}

}  // namespace gt4::saves
