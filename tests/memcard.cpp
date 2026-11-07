// runtime/save_folder.cpp, the cards behind runtime/memcard.cpp's sceMc* calls, on folders made here: a save
// written and read back; a write cut halfway (the session ends before the close) leaves the old file whole and its
// copy goes at the next start; the copies and backups stay off the card the game sees; session backups rotate;
// the second slot. Works in the folder given as the first argument (the build folder's), not a shared one.
#include "save_folder.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace gt4::mc;

namespace {

int failures = 0;
void expect(bool ok, const char *what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

void writeFile(const fs::path &p, const std::string &d) {
    std::ofstream f(p, std::ios::binary);
    f.write(d.data(), static_cast<std::streamsize>(d.size()));
}

std::string readFile(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// every entry under `dir`, recursively, as relative paths
std::vector<std::string> tree(const fs::path &dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        out.push_back(fs::relative(it->path(), dir).generic_string());
    return out;
}

bool hasTemp(const fs::path &dir) {
    for (const std::string &e : tree(dir))
        if (e.size() > 4 && e.compare(e.size() - 4, 4, ".tmp") == 0) return true;
    return false;
}

std::vector<std::string> listing(Cards &cards, int port, const char *pattern) {
    std::vector<std::string> names;
    McDirEntry table[8];
    int n = cards.getdir(port, pattern, 0, 8, table);
    while (n > 0) {
        for (int k = 0; k < n; ++k) names.emplace_back(table[k].name);
        n = cards.getdir(port, pattern, 1, 8, table);
    }
    return names;
}

bool listed(const std::vector<std::string> &names, const std::string &name) {
    for (const std::string &n : names)
        if (n == name) return true;
    return false;
}

std::vector<fs::path> backups(const fs::path &card) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto &e : fs::directory_iterator(card / ".backup", ec)) out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

int writeWhole(Cards &cards, int port, const char *name, const std::string &bytes) {
    const int fd = cards.open(port, name, kOpenWrite | kOpenCreate);
    if (fd < 0) return fd;
    if (cards.write(fd, bytes.data(), static_cast<int>(bytes.size())) != static_cast<int>(bytes.size())) return -100;
    return cards.close(fd);
}

// a save's bytes, every version 3000 long: opening a card file for writing does not shorten it (libmc has no
// truncation), so a shorter version would keep the old one's tail
std::string save(const std::string &tag) {
    std::string s = tag;
    s.resize(3000, '.');
    return s;
}

const char kSave[] = "BASCUS-97328GAMEDATA";
const char kFile[] = "/BASCUS-97328GAMEDATA/BASCUS-97328GAMEDATA";

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: gt4_memcard_test WORK_FOLDER\n");
        return 2;
    }
    const fs::path work = fs::u8path(argv[1]);
    std::error_code ec;
    fs::remove_all(work, ec);
    const fs::path card = work / "saves";
    fs::create_directories(card / kSave);
    const std::string oldSave(3000, 'o'), newSave = save("new");
    writeFile(card / kSave / kSave, oldSave);
    writeFile(card / kSave / "icon.sys", std::string(964, 'i'));

    {   // a save written whole replaces the file at close; nothing else is left on the disk
        Cards cards;
        cards.insert(0, card, 0);
        expect(cards.newCardResult(0) == -1 && cards.newCardResult(0) == 0, "a new card once a session");
        expect(writeWhole(cards, 0, kFile, newSave) == 0, "write a save");
        expect(readFile(card / kSave / kSave) == newSave, "the save has the new bytes");
        expect(!hasTemp(card), "no copy left after the close");
        expect(!fs::exists(card / ".backup"), "no backup when backups are off");
        // reading back through the card
        const int fd = cards.open(0, kFile, kOpenRead);
        std::vector<char> back(4000);
        expect(fd >= 0 && cards.seek(fd, 0, 2) == 3000 && cards.seek(fd, 0, 0) == 0, "seek to the end gives the size");
        expect(cards.read(fd, back.data(), 4000) == 3000 && std::memcmp(back.data(), newSave.data(), 3000) == 0, "read back");
        expect(cards.write(fd, "x", 1) == 0, "no write through a file opened for reading");
        expect(cards.close(fd) == 0 && readFile(card / kSave / kSave) == newSave, "a read leaves the file");
        // rewriting part of a file in place keeps the rest, as on a card
        const int rw = cards.open(0, kFile, kOpenWrite);
        expect(rw >= 0 && cards.seek(rw, 10, 0) == 10 && cards.write(rw, "abc", 3) == 3 && cards.close(rw) == 0, "write in place");
        std::string patched = newSave;
        patched.replace(10, 3, "abc");
        expect(readFile(card / kSave / kSave) == patched, "a write in place changes only its bytes");
        writeFile(card / kSave / kSave, oldSave);
    }

    {   // a write cut halfway: the session ends before the close
        Cards cards;
        cards.insert(0, card, 0);
        const int fd = cards.open(0, kFile, kOpenWrite);
        expect(fd >= 0, "open the save for writing");
        expect(cards.write(fd, newSave.data(), 1200) == 1200, "write half");
        expect(cards.flush(fd) == 0, "flush");
        const int created = cards.open(0, "/BASCUS-97328GAMEDATA/garage", kOpenWrite | kOpenCreate);
        expect(created >= 0 && cards.write(created, "g", 1) == 1, "a new file, half made");
        // the game's view in the meantime: no copies listed
        const std::vector<std::string> names = listing(cards, 0, "/BASCUS-97328GAMEDATA/*");
        expect(names.size() == 4 && listed(names, ".") && listed(names, "..") && listed(names, kSave) && listed(names, "icon.sys"),
               "the save's listing shows its own files only");
        cards.abandon();
        expect(readFile(card / kSave / kSave) == oldSave, "the old save is whole after a cut write");
        expect(!fs::exists(card / kSave / "garage"), "a file made in a cut write does not appear");
        expect(hasTemp(card), "the cut write's copies are on the disk");
    }
    {   // the next session removes them
        Cards cards;
        cards.insert(0, card, 0);
        expect(!hasTemp(card), "copies of a cut write removed at the next start");
        expect(readFile(card / kSave / kSave) == oldSave, "and the save is still the old one");
    }

    {   // hidden names: off the listing, the free space and the paths
        fs::create_directories(card / ".backup" / "20260101-000000" / kSave);
        writeFile(card / ".backup" / "20260101-000000" / kSave / kSave, std::string(200000, 'b'));
        Cards cards;
        cards.insert(0, card, 0);
        const std::vector<std::string> root = listing(cards, 0, "/*");
        expect(root.size() == 1 && root[0] == kSave, "the root lists the save only");
        expect(cards.freeClusters(0) == kCardClusters - (1 + 1 + 3 + 1 + 1), "the free space counts the save only");
        expect(cards.open(0, "/.backup/20260101-000000/BASCUS-97328GAMEDATA/BASCUS-97328GAMEDATA", kOpenRead) == kNoEntry,
               "a backup is not reachable by path");
        expect(cards.mkdir(0, "/.x") == kNoEntry && cards.chdir(0, "/.backup", nullptr) == kNoEntry, "no hidden folders");
        expect(cards.rename(0, kFile, ".hidden") == kDenied, "no rename to a hidden name");
        fs::remove_all(card / ".backup");
    }

    {   // session backups: one before the first change of a session, the newest N kept
        for (int session = 0; session < 4; ++session) {
            Cards cards;
            cards.insert(0, card, 2);
            const int fd = cards.open(0, kFile, kOpenRead);  // reads change nothing: no backup
            cards.close(fd);
            expect(cards.sessionBackup(0).empty(), "no backup for a session that only reads");
            const std::string bytes = save("session " + std::to_string(session));
            expect(writeWhole(cards, 0, kFile, bytes) == 0, "a session's save");
            const fs::path b = cards.sessionBackup(0);
            expect(!b.empty() && readFile(b / kSave / kSave) == (session == 0 ? oldSave : save("session " + std::to_string(session - 1) + "b")),
                   "the backup holds the save as the session found it");
            expect(readFile(b / kSave / "icon.sys") == std::string(964, 'i'), "the backup holds every file of the save");
            expect(writeWhole(cards, 0, kFile, save("session " + std::to_string(session) + "b")) == 0, "a second save in the session");
            expect(backups(card).size() == static_cast<size_t>(std::min(session + 1, 2)), "the newest two backups are kept");
        }
        const std::vector<fs::path> kept = backups(card);
        expect(kept.size() == 2 && readFile(kept.back() / kSave / kSave) == save("session 2b"), "the newest backup is the last session's start");
        expect(readFile(card / kSave / kSave) == save("session 3b"), "the save is the last one written");
        {   // a backup cut halfway is removed at the next start
            fs::create_directories(card / ".backup" / ".20260101-000000");
            Cards cards;
            cards.insert(0, card, 2);
            expect(!fs::exists(card / ".backup" / ".20260101-000000"), "a backup half made is removed");
            expect(backups(card).size() == 2, "the whole backups stay");
        }
        {   // mkdir, delete and rename are changes too
            Cards cards;
            cards.insert(0, card, 5);
            expect(cards.mkdir(0, "/BASCUS-97328REPLAY") == 0 && !cards.sessionBackup(0).empty(), "mkdir backs up first");
            expect(cards.remove(0, "/BASCUS-97328REPLAY") == 0, "delete an empty folder");
            expect(backups(card).size() == 3, "one backup a session");
        }
    }

    {   // the second slot: empty unless a folder is given
        Cards cards;
        cards.insert(0, card, 0);
        cards.insert(1, fs::path(), 0);
        expect(!cards.present(1) && cards.open(1, kFile, kOpenRead) == kNoCard && cards.mkdir(1, "/X") == kNoCard,
               "no card in slot 2");
        const fs::path second = work / "saves2";
        cards.insert(1, second, 0);
        expect(cards.present(1) && fs::is_directory(second), "a card in slot 2");
        expect(cards.mkdir(1, "/BASCUS-97328GAMEDATA") == 0 && writeWhole(cards, 1, kFile, "two") == 0, "a save on card 2");
        expect(readFile(second / kSave / kSave) == "two" && readFile(card / kSave / kSave) == save("session 3b"),
               "each card keeps its own saves");
        expect(cards.chdir(1, "/BASCUS-97328GAMEDATA", nullptr) == 0 && listing(cards, 0, "*").size() == 1,
               "each card has its own current folder");
    }

    if (failures) return 1;
    fs::remove_all(work, ec);
    std::printf("memcard: all checks passed\n");
    return 0;
}
