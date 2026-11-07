#pragma once
// The memory cards behind libmc, as folders on the host (docs/SAVES.md); runtime/memcard.cpp is the sceMc* face
// of this. Plain C++ (no guest, kernel or log headers), so tests/memcard.cpp runs it on the host.
//
// A card is a folder: GT4's save folders (BASCUS-97328...) in it as directories, their files as files. Results
// follow libmc: >= 0 success (a descriptor, a count, a position), kNoEntry, kDenied, kNotEmpty, kTooMany, kNoCard.
//
// Safety, none of which the game can see:
// - a file opened for writing is written to a temporary copy beside it (".NAME.tmp") and replaces the file only
//   when it is closed (flushed to the disk, then renamed over it): a session that ends halfway through a save
//   leaves the old file whole, and the copy is removed at the next start;
// - before the first change a session makes to a card, the card's saves are copied to
//   .backup/YYYYMMDD-HHMMSS/ inside it; the newest `backups` copies are kept;
// - names starting with "." (the temporary copies, .backup) are not on the card: not listed, not counted in the
//   free space, not reachable by path.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace gt4::mc {

constexpr int kNoCard = -10, kNoEntry = -4, kDenied = -5, kNotEmpty = -6, kTooMany = -7;
constexpr int kCardClusters = 8000;  // usable 1 KB clusters of an 8 MB card
constexpr int kOpenRead = 0x1, kOpenWrite = 0x2, kOpenCreate = 0x200;
constexpr int kPorts = 2, kFiles = 16;

struct McDateTime {
    uint8_t resv, sec, min, hour, day, month;
    uint16_t year;
};
struct McDirEntry {
    McDateTime created, modified;
    uint32_t size;
    uint16_t attr, resv1;
    uint32_t resv2, pda;
    char name[32];
};
static_assert(sizeof(McDirEntry) == 64, "sceMcTblGetDir");

class Cards {
public:
    using Log = void (*)(const char *line);

    // The folder of the card in `port` (0 or 1); an empty path leaves the slot empty. Removes temporary copies a
    // previous session left. `backups` is how many session backups to keep (0: none made).
    void insert(int port, const std::filesystem::path &folder, int backups);
    bool present(int port) const { return port >= 0 && port < kPorts && !card[port].root.empty(); }
    void setLog(Log l) { log = l; }

    int freeClusters(int port);
    // -1 at the first call for a card in this session ("a new card"), 0 after.
    int newCardResult(int port);

    int open(int port, const char *name, int mode);
    int close(int fd);
    int seek(int fd, int offset, int origin);
    int read(int fd, void *buf, int size);
    int write(int fd, const void *buf, int size);
    int flush(int fd);
    int mkdir(int port, const char *name);
    int chdir(int port, const char *name, std::string *previous);
    // mode 0 starts a listing of `name` (a pattern in its last part: "/BASCUS-97328*", "/DIR/*"); every call
    // returns the next entries, at most maxent.
    int getdir(int port, const char *name, unsigned mode, int maxent, McDirEntry *table);
    int remove(int port, const char *name);
    int rename(int port, const char *from, const char *to);

    // The descriptors' files are dropped without being closed: what a session that ends here leaves (tests).
    void abandon();
    // The backup folder of this session for the card in `port` ("" until one is made).
    std::filesystem::path sessionBackup(int port) const { return present(port) ? card[port].backup : std::filesystem::path(); }

private:
    struct Card {
        std::filesystem::path root;
        std::string cwd = "/";
        bool reportedNew = false;
        int backups = 0;
        bool backedUp = false;
        std::filesystem::path backup;
        std::vector<McDirEntry> listing;
        size_t next = 0;
    };
    struct File {
        FILE *f = nullptr;
        int port = 0;
        bool writing = false, written = false;
        std::filesystem::path path, temp;
    };
    Card card[kPorts];
    File files[kFiles];
    Log log = nullptr;

    std::string cardPath(const Card &c, const char *name) const;
    std::filesystem::path hostPath(const Card &c, const std::string &cardPath) const;
    void say(const std::string &line) const;
    void backUp(int port);
    File *file(int fd) { return fd >= 0 && fd < kFiles && files[fd].f ? &files[fd] : nullptr; }
};

}  // namespace gt4::mc
