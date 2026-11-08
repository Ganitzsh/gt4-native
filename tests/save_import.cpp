// platform/saves/save_import.cpp on a small memory card image built here (with and without ECC), on a .psu export
// and on a folder card: the GT4 save comes out with its files, others are left.
#include "save_import.h"
#include "temp_path.h"

#include <algorithm>
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
    write(2, entry(0x8427, 0, 2, "."), 0);  // as on real cards: a folder's count is in its parent's entry
    write(2, entry(0x8427, 0, 0, ".."), 512);
    write(3, entry(0x8497, static_cast<uint32_t>(file.size()), 4, "BASCUS-97328GAMEDATA"), 0);
    std::memcpy(plain.data() + at(alloc + 4), file.data(), file.size());
    write(6, entry(0x8427, 0, 6, "."), 0);
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
    const fs::path dir = gt4::test::uniqueTempPath("gt4_save_import_test");
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
        // again, twice: each save an import replaces is kept in .backup/imports/DATE/, off the save listing
        writeFile(saves / "BASCUS-97328GAMEDATA" / "BASCUS-97328GAMEDATA", payload(10, 7));
        const gt4::saves::ImportResult again = gt4::saves::importSave(image, saves);
        const gt4::saves::ImportResult third = gt4::saves::importSave(image, saves);
        std::vector<fs::path> kept;
        for (const auto& e : fs::directory_iterator(saves / ".backup" / "imports")) kept.push_back(e.path());
        std::sort(kept.begin(), kept.end());
        expect(again.ok && third.ok && kept.size() == 2, "each replaced save kept, in its own folder");
        expect(kept.size() == 2 && readFile(kept[0] / "BASCUS-97328GAMEDATA" / "BASCUS-97328GAMEDATA") == payload(10, 7),
               "the save the import replaced is whole");
        size_t listed = 0;
        for (const auto& e : fs::directory_iterator(saves))
            listed += e.path().filename().u8string().rfind(u8"BASCUS-97328", 0) == 0;
        expect(listed == 1, "no displaced save among the game's saves");
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

    // an export as .psu reads back as the same save, files and bytes
    {
        const fs::path saves = dir / "saves_psu";
        writeFile(saves / "BASCUS-97328GAMEDATA" / "icon.sys", payload(964, 4));
        writeFile(saves / "BASCUS-97328GAMEDATA" / ".garage.3.tmp", payload(5, 5));   // not the game's
        const gt4::saves::ExportResult e = gt4::saves::exportSave(saves, "BASCUS-97328GAMEDATA", dir / "out.psu");
        expect(e.ok, "export a save");
        if (!e.ok) std::fprintf(stderr, "  %s\n", e.message.c_str());
        std::string error;
        const std::vector<gt4::saves::SaveEntry> back = gt4::saves::readSaves(dir / "out.psu", error);
        expect(error.empty() && back.size() == 1 && back[0].folder == "BASCUS-97328GAMEDATA" && back[0].files.size() == 2,
               "the export reads back with the save's files only");
        if (back.size() == 1 && back[0].files.size() == 2) {
            expect(back[0].files[0].first == "garage" && back[0].files[0].second == file, "the garage's bytes");
            expect(back[0].files[1].first == "icon.sys" && back[0].files[1].second == payload(964, 4), "the icon's bytes");
        }
        expect(fs::file_size(dir / "out.psu") % 1024 == 512, "512-byte entries and 1024-byte data units");
        expect(!gt4::saves::exportSave(saves, "BASCUS-97328NONE", dir / "none.psu").ok && !fs::exists(dir / "none.psu"),
               "no export of a save that is not there");
        expect(!gt4::saves::exportSave(saves, ".backup", dir / "hidden.psu").ok, "no export of a hidden folder");
    }

    // an export as a PCSX2 .ps2 card: 8 MiB of 512-byte pages with 16 bytes of ECC each, formatted, every GT4 save on
    // it and nothing else, read back by the card reader file for file (sizes like the game's: the garage spans
    // over a thousand clusters)
    {
        const fs::path saves = dir / "saves_card";
        for (const char* d : {"BASCUS-97328GAMEDATA", "BASCUS-97328REPLAY", "BASLUS-00000OTHER"}) fs::create_directories(saves / d);
        const std::vector<unsigned char> data = payload(237760, 11), garage = payload(1280064, 12);
        writeFile(saves / "BASCUS-97328GAMEDATA" / "BASCUS-97328GAMEDATA", data);
        writeFile(saves / "BASCUS-97328GAMEDATA" / "garage", garage);
        writeFile(saves / "BASCUS-97328GAMEDATA" / "icon.sys", payload(964, 13));
        writeFile(saves / "BASCUS-97328GAMEDATA" / "icon0.ico", payload(9796, 14));
        writeFile(saves / "BASCUS-97328GAMEDATA" / ".garage.1.tmp", payload(5, 5));   // not the game's
        writeFile(saves / "BASCUS-97328REPLAY" / "replay", payload(3000, 15));
        writeFile(saves / "BASLUS-00000OTHER" / "x", payload(10, 16));                // another game's
        const gt4::saves::ExportResult e = gt4::saves::exportCard(saves, dir / "out.ps2");
        expect(e.ok, "export a card");
        if (!e.ok) std::fprintf(stderr, "  %s\n", e.message.c_str());
        const std::vector<unsigned char> image = readFile(dir / "out.ps2");
        expect(image.size() == 8650752, "8 MiB with ECC: 16384 pages of 528 bytes");
        expect(image.size() > 40 && std::memcmp(image.data(), "Sony PS2 Memory Card Format 1.2.0.0", 35) == 0,
               "the superblock");
        // an empty page's ECC: each 128-byte quarter of zeros gives 77 7F 7F, then four zero bytes
        if (image.size() == 8650752) {
            const unsigned char* spare = image.data() + 16383 * 528 + 512;
            bool zeros = true;
            for (int q = 0; q < 4; ++q)
                zeros &= spare[3 * q] == 0x77 && spare[3 * q + 1] == 0x7F && spare[3 * q + 2] == 0x7F;
            zeros &= spare[12] == 0 && spare[13] == 0 && spare[14] == 0 && spare[15] == 0;
            expect(zeros, "an empty page's ECC");
            // backup block 1022 erased, data and spare (as mymcplus formats it; not erased, the card's server copies it
            // back at mount as an unfinished write); block 1023 zero pages with their ECC
            bool erased = true;
            for (size_t b = 16352 * 528; b < 16368 * 528; ++b) erased &= image[b] == 0xFF;
            expect(erased, "backup block 1022 erased");
            expect(image[16368 * 528] == 0 && image[16368 * 528 + 512] == 0x77 && image[16351 * 528 + 512] == 0x77,
                   "the pages around it written");
        }
        // a save folder's "." points back at the folder's place in its parent (the root's first cluster, its entry
        // there), as the card's server follows it when a file is written: not at the folder itself
        if (image.size() == 8650752) {
            const auto page = [&](uint32_t cluster, uint32_t half) { return image.data() + (cluster * 2 + half) * 528; };
            const auto u32at = [](const unsigned char* p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16) | uint32_t(p[3]) << 24; };
            const uint32_t alloc = u32at(image.data() + 0x34), root = u32at(image.data() + 0x3C);
            // the root's third entry: two entries a cluster, so the first page of the root's second cluster (the writer
            // lays a chain down contiguously)
            const unsigned char* folderEntry = page(alloc + root + 1, 0);
            const uint32_t folderCluster = u32at(folderEntry + 0x10);
            const unsigned char* dot = page(alloc + folderCluster, 0);
            expect(std::string(reinterpret_cast<const char*>(folderEntry + 0x40)) == "BASCUS-97328GAMEDATA" &&
                       std::string(reinterpret_cast<const char*>(dot + 0x40)) == "." && u32at(dot + 0x10) == root &&
                       u32at(dot + 0x14) == 2,
                   "a folder's . points at its place in the root");
        }
        std::string error;
        const std::vector<gt4::saves::SaveEntry> back = gt4::saves::readSaves(dir / "out.ps2", error);
        expect(error.empty() && back.size() == 2 && back[0].folder == "BASCUS-97328GAMEDATA" &&
                   back[1].folder == "BASCUS-97328REPLAY",
               "the card holds GT4's two saves only");
        if (back.size() == 2 && back[0].files.size() == 4) {
            expect(back[0].files[0].first == "BASCUS-97328GAMEDATA" && back[0].files[0].second == data, "the game data's bytes");
            expect(back[0].files[1].first == "garage" && back[0].files[1].second == garage, "the garage's bytes");
            expect(back[0].files[3].first == "icon0.ico" && back[0].files[3].second == payload(9796, 14), "the icon's bytes");
        } else {
            expect(false, "the game data's four files");
        }
        const gt4::saves::ImportResult again = gt4::saves::importSave(dir / "out.ps2", dir / "saves_card_back");
        expect(again.ok && readFile(dir / "saves_card_back" / "BASCUS-97328GAMEDATA" / "garage") == garage,
               "the card imports back");
        expect(!gt4::saves::exportCard(dir / "saves_folder_none", dir / "none.ps2").ok && !fs::exists(dir / "none.ps2"),
               "no card without a GT4 save");
        // an existing file is never opened: refused, or with force replaced by a new file renamed over it
        const std::vector<unsigned char> mine = {'m', 'i', 'n', 'e'};
        writeFile(dir / "mine.ps2", mine);
        writeFile(dir / "mine.psu", mine);
        expect(!gt4::saves::exportCard(saves, dir / "mine.ps2").ok && readFile(dir / "mine.ps2") == mine &&
                   !gt4::saves::exportSave(saves, "BASCUS-97328GAMEDATA", dir / "mine.psu").ok &&
                   readFile(dir / "mine.psu") == mine,
               "an existing card or .psu refused, untouched");
        error.clear();   // (the times in the new files may differ from the first export's: compared by what they hold)
        expect(gt4::saves::exportCard(saves, dir / "mine.ps2", true).ok &&
                   readFile(dir / "mine.ps2").size() == image.size() &&
                   gt4::saves::readSaves(dir / "mine.ps2", error).size() == 2 &&
                   gt4::saves::exportSave(saves, "BASCUS-97328GAMEDATA", dir / "mine.psu", true).ok &&
                   gt4::saves::readSaves(dir / "mine.psu", error).size() == 1 && error.empty(),
               "with force: replaced by the new export");
        expect(!fs::exists(dir / ".mine.ps2.tmp") && !fs::exists(dir / ".mine.psu.tmp"), "no temporary file left");
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
        writeFile(dir / "blank.ps2", std::vector<unsigned char>(8192, 0xFF));
        const gt4::saves::ImportResult blank = gt4::saves::importSave(dir / "blank.ps2", dir / "saves_blank");
        expect(!blank.ok && blank.message.find("unformatted") != std::string::npos, "an unformatted card named as such");
    }

    fs::remove_all(dir, ec);
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("save import: all checks passed\n");
    return 0;
}
