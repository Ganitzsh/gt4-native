// platform/saves/save_import.cpp on a small memory card image built here (with and without ECC), on a .psu export
// and on a folder card: the GT4 save comes out with its files, others are left.
#include "save_import.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

void put16(std::vector<unsigned char>& d, size_t at, uint16_t v) { d[at] = v & 0xFF; d[at + 1] = v >> 8; }
void put32(std::vector<unsigned char>& d, size_t at, uint32_t v) {
    for (int k = 0; k < 4; ++k) d[at + k] = static_cast<unsigned char>(v >> (8 * k));
}

// A 512-byte directory entry.
std::vector<unsigned char> entry(uint16_t mode, uint32_t length, uint32_t cluster, const char* name) {
    std::vector<unsigned char> e(512, 0);
    put16(e, 0, mode);
    put32(e, 4, length);
    put32(e, 0x10, cluster);
    std::memcpy(e.data() + 0x40, name, std::strlen(name));
    return e;
}

std::vector<unsigned char> payload(size_t n, int seed) {
    std::vector<unsigned char> p(n);
    for (size_t k = 0; k < n; ++k) p[k] = static_cast<unsigned char>(k * 7 + seed);
    return p;
}

// A card of 64 clusters (1 KiB each): superblock in cluster 0, the indirect FAT cluster 2, the FAT cluster 3,
// allocatable clusters from 8. Root (allocatable 0-1): ".", "..", the GT4 save, another game's save; GT4's
// folder (2-3): ".", "..", a 1500-byte file (4-5); the other's folder (6): ".", "..".
std::vector<unsigned char> card(bool ecc, const std::vector<unsigned char>& file) {
    const uint32_t clusters = 64, alloc = 8, page = 512, spare = ecc ? 16 : 0;
    std::vector<unsigned char> plain(clusters * 1024, 0xFF);
    auto at = [&](uint32_t cluster) { return static_cast<size_t>(cluster) * 1024; };
    std::memset(plain.data(), 0, 1024);
    std::memcpy(plain.data(), "Sony PS2 Memory Card Format 1.2.0.0", 35);
    put16(plain, 0x28, 512);
    put16(plain, 0x2A, 2);
    put16(plain, 0x2C, 16);
    put32(plain, 0x30, clusters);
    put32(plain, 0x34, alloc);
    put32(plain, 0x3C, 0);
    put32(plain, 0x50, 2);   // the indirect FAT cluster
    std::memset(plain.data() + at(2), 0, 1024);
    put32(plain, at(2), 3);  // -> the FAT cluster
    std::memset(plain.data() + at(3), 0, 1024);
    const uint32_t end = 0xFFFFFFFFu;
    const uint32_t chain[] = {0x80000001u, end, 0x80000003u, end, 0x80000005u, end, end};
    for (uint32_t k = 0; k < 7; ++k) put32(plain, at(3) + 4 * k, chain[k]);
    auto write = [&](uint32_t allocCluster, const std::vector<unsigned char>& bytes, size_t offset) {
        std::memcpy(plain.data() + at(alloc + allocCluster) + offset, bytes.data(), bytes.size());
    };
    write(0, entry(0x8427, 4, 0, "."), 0);
    write(0, entry(0x8427, 0, 0, ".."), 512);
    write(1, entry(0x8427, 3, 2, "BASCUS-97328GAMEDATA"), 0);
    write(1, entry(0x8427, 2, 6, "BASLUS-00000OTHER"), 512);
    write(2, entry(0x8427, 3, 2, "."), 0);
    write(2, entry(0x8427, 0, 0, ".."), 512);
    write(3, entry(0x8497, static_cast<uint32_t>(file.size()), 4, "BASCUS-97328GAMEDATA"), 0);
    std::memcpy(plain.data() + at(alloc + 4), file.data(), file.size());
    write(6, entry(0x8427, 2, 6, "."), 0);
    write(6, entry(0x8427, 0, 0, ".."), 512);
    std::vector<unsigned char> out;
    for (size_t p = 0; p < plain.size() / page; ++p) {
        out.insert(out.end(), plain.begin() + static_cast<std::ptrdiff_t>(p * page), plain.begin() + static_cast<std::ptrdiff_t>((p + 1) * page));
        out.insert(out.end(), spare, 0);
    }
    return out;
}

void writeFile(const fs::path& p, const std::vector<unsigned char>& d) {
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
}

std::vector<unsigned char> readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

}  // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "gt4_save_import_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const std::vector<unsigned char> file = payload(1500, 3);

    for (bool ecc : {true, false}) {
        const fs::path image = dir / (ecc ? "card.ps2" : "card.bin");
        writeFile(image, card(ecc, file));
        const fs::path saves = dir / (ecc ? "saves_ecc" : "saves_plain");
        const gt4::saves::ImportResult r = gt4::saves::importSave(image, saves);
        expect(r.ok, ecc ? "card with ECC imported" : "card without ECC imported");
        if (!r.ok) std::fprintf(stderr, "  %s\n", r.message.c_str());
        expect(r.imported.size() == 1 && r.imported[0] == "BASCUS-97328GAMEDATA", "only GT4's save taken");
        expect(readFile(saves / "BASCUS-97328GAMEDATA" / "BASCUS-97328GAMEDATA") == file, "the save's file read across two clusters");
        expect(!fs::exists(saves / "BASLUS-00000OTHER"), "another game's save left");
        // again: the first import is kept beside the new one
        const gt4::saves::ImportResult again = gt4::saves::importSave(image, saves);
        size_t kept = 0;
        for (const auto& e : fs::directory_iterator(saves))
            kept += std::string(reinterpret_cast<const char*>(e.path().filename().u8string().c_str())).rfind("BASCUS-97328GAMEDATA.before-import-", 0) == 0;
        expect(again.ok && kept == 1, "an existing save kept beside the import");
    }

    // a .psu: the folder, ".", "..", the file and its data in 1024-byte units
    {
        std::vector<unsigned char> psu;
        auto add = [&](const std::vector<unsigned char>& e) { psu.insert(psu.end(), e.begin(), e.end()); };
        add(entry(0x8427, 3, 0, "BASCUS-97328GAMEDATA"));
        add(entry(0x8427, 0, 0, "."));
        add(entry(0x8427, 0, 0, ".."));
        add(entry(0x8497, static_cast<uint32_t>(file.size()), 0, "garage"));
        psu.insert(psu.end(), file.begin(), file.end());
        psu.resize(psu.size() + (1024 - file.size() % 1024) % 1024, 0);
        writeFile(dir / "save.psu", psu);
        const gt4::saves::ImportResult r = gt4::saves::importSave(dir / "save.psu", dir / "saves_psu");
        expect(r.ok && readFile(dir / "saves_psu" / "BASCUS-97328GAMEDATA" / "garage") == file, ".psu imported");
    }

    // a folder card
    {
        const fs::path folder = dir / "foldercard" / "BASCUS-97328GAMEDATA";
        fs::create_directories(folder);
        writeFile(folder / "garage", file);
        writeFile(folder / "_pcsx2_index", payload(64, 1));
        const gt4::saves::ImportResult r = gt4::saves::importSave(dir / "foldercard", dir / "saves_folder");
        expect(r.ok && readFile(dir / "saves_folder" / "BASCUS-97328GAMEDATA" / "garage") == file, "folder card imported");
        expect(!fs::exists(dir / "saves_folder" / "BASCUS-97328GAMEDATA" / "_pcsx2_index"), "PCSX2's index files left");
    }

    // not a card
    {
        writeFile(dir / "junk.ps2", payload(4096, 9));
        expect(!gt4::saves::importSave(dir / "junk.ps2", dir / "saves_junk").ok, "a file that is no card refused");
    }

    fs::remove_all(dir, ec);
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("save import: all checks passed\n");
    return 0;
}
