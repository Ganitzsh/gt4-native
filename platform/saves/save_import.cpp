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
    const std::string when = stamp();
    for (const SaveEntry& save : saves) {
        const fs::path dir = saveFolder / fs::u8path(save.folder);
        if (fs::exists(dir, ec)) fs::rename(dir, saveFolder / fs::u8path(save.folder + ".before-import-" + when), ec);
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

}  // namespace gt4::saves
