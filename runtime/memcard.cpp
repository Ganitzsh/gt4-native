// The native save folder (docs/SAVES.md) behind libmc: the SDK's sceMc* calls, replaced at their API, on files.
//
// The game's card manager (gt4-decomp unit 005547E8, 00561E00-00562738) issues one libmc call at a
// time and completes it with sceMcSync; each call here does its work at once and leaves the result
// for the next sceMcSync, as an IOP that answered instantly would. The card holds PS2-style
// directories (BASCUS-97328GAMEDATA...) as host directories and files as host files, so saves can
// be copied in and out, and saves from a PS2 come in with gt4_host --import-save. What the game sees of a card is
// what its save code asks: one in the first slot, always present and formatted; the second slot is empty.
//
// libmc conventions followed: results 0 success, -1 "new card" (the first GetInfo of a session),
// -4 no such entry, -5 not permitted, -6 directory not empty, -7 too many open files, -10 no card;
// sceMcGetDir entries are 64 bytes (creation and modification time, size, attributes, name); free
// space is counted in 1 KB clusters of an 8 MB card.
#include "guest.h"
#include "kernel.h"
#include "log.h"
#include "runtime.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

using namespace gt4::rt;
namespace fs = std::filesystem;

namespace {

constexpr int kNoCard = -10, kNoEntry = -4, kDenied = -5, kNotEmpty = -6, kTooMany = -7;
constexpr int kCardClusters = 8000;  // usable 1 KB clusters of an 8 MB card
constexpr int kOpenRead = 0x1, kOpenWrite = 0x2, kOpenCreate = 0x200;

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

struct State {
    bool pending = false;
    int cmd = 0, result = 0;
    bool reported_new = false;
    std::string cwd = "/";
    FILE *files[16] = {};
} g;

const bool g_trace = getenv("GT4_TRACE_MC") != nullptr;  // log every call and its result

int finish(int cmd, int result) {
    if (g_trace) log_info("memcard: command %d -> %d", cmd, result);
    g.cmd = cmd;
    g.result = result;
    g.pending = true;
    return 0;
}

fs::path root() { return fs::u8path(g_config.memcard_path); }

// a card path ("/DIR/FILE", "FILE" relative to the current directory) on the host; "" if it leaves
// the card
std::string card_path(const char *name) {
    std::string p = name ? name : "";
    if (p.empty() || p[0] != '/') p = g.cwd + (g.cwd.back() == '/' ? "" : "/") + p;
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= p.size()) {
        const size_t j = p.find('/', i);
        const std::string part = p.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        if (j == std::string::npos) break;
        i = j + 1;
    }
    std::string out = "/";
    for (size_t k = 0; k < parts.size(); ++k) out += (k ? "/" : "") + parts[k];
    return out;
}

fs::path host_path(const std::string &card) { return root() / fs::u8path(card.substr(1)); }

McDateTime to_ps2_time(const fs::path &p) {
    // the host file's modification time as calendar time in JST, as the PS2 clock keeps it (UTC + 9)
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(p.wstring().c_str(), GetFileExInfoStandard, &a)) return {};
    const uint64_t ft = (static_cast<uint64_t>(a.ftLastWriteTime.dwHighDateTime) << 32) | a.ftLastWriteTime.dwLowDateTime;
    const std::time_t tt = static_cast<std::time_t>(ft / 10000000ULL - 11644473600ULL) + 9 * 3600;
    std::tm tm{};
    gmtime_s(&tm, &tt);
    McDateTime d{};
    d.sec = static_cast<uint8_t>(tm.tm_sec);
    d.min = static_cast<uint8_t>(tm.tm_min);
    d.hour = static_cast<uint8_t>(tm.tm_hour);
    d.day = static_cast<uint8_t>(tm.tm_mday);
    d.month = static_cast<uint8_t>(tm.tm_mon + 1);
    d.year = static_cast<uint16_t>(tm.tm_year + 1900);
    return d;
}

void fill_entry(McDirEntry &e, const std::string &name, const fs::path &p) {
    std::memset(&e, 0, sizeof e);
    std::error_code ec;
    const bool dir = fs::is_directory(p, ec);
    e.created = e.modified = to_ps2_time(p);
    e.size = dir ? 0 : static_cast<uint32_t>(fs::file_size(p, ec));
    e.attr = dir ? 0x8427 : 0x8497;  // exists, readable, writable, executable, (sub)directory or file
    std::snprintf(e.name, sizeof e.name, "%s", name.c_str());
}

uint64_t used_clusters() {
    uint64_t kb = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root(), ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        kb += 1;  // every entry takes a cluster
        if (it->is_regular_file(ec)) kb += (it->file_size(ec) + 1023) / 1024;
    }
    return kb;
}

// "*" and "?" in the last part of a GetDir pattern
bool match(const char *pat, const char *s) {
    if (!*pat) return !*s;
    if (*pat == '*') return match(pat + 1, s) || (*s && match(pat, s + 1));
    if (*pat == '?' || std::tolower(static_cast<unsigned char>(*pat)) == std::tolower(static_cast<unsigned char>(*s)))
        return *s && match(pat + 1, s + 1);
    return false;
}

struct DirListing {
    std::vector<McDirEntry> entries;
    size_t next = 0;
} g_listing;

}  // namespace

extern "C" {

int gt4_replace_sceMcInit() {
    std::error_code ec;
    fs::create_directories(root(), ec);
    log_info("memcard: slot 1 is %s", g_config.memcard_path);
    return 0;
}
int gt4_replace_sceMcEnd() { return 0; }
int gt4_replace_sceMcChangeThreadPriority(int) { return 0; }
int gt4_replace_sceMcGetSlotMax(int) { return 1; }
int gt4_replace_sceMcUdCheckNewCard() { return 0; }

int gt4_replace_sceMcGetInfo(int port, int, int *type, int *free_kb, int *format) {
    KernelScope ks;
    if (port != 0) {
        if (type) *type = 0;
        if (free_kb) *free_kb = 0;
        if (format) *format = 0;
        return finish(1, kNoCard);
    }
    if (type) *type = 2;  // PS2 memory card
    if (format) *format = 1;
    if (free_kb) *free_kb = static_cast<int>(std::max<int64_t>(0, kCardClusters - static_cast<int64_t>(used_clusters())));
    const int result = g.reported_new ? 0 : -1;
    g.reported_new = true;
    return finish(1, result);
}

int gt4_replace_sceMcOpen(int port, int, const char *name, int mode) {
    KernelScope ks;
    if (g_trace) log_info("memcard: open %s mode %x", name, mode);
    if (port != 0) return finish(2, kNoCard);
    const std::string card = card_path(name);
    const fs::path p = host_path(card);
    std::error_code ec;
    if (fs::is_directory(p, ec)) return finish(2, kDenied);
    const bool exists = fs::exists(p, ec);
    if (!exists && !(mode & kOpenCreate)) return finish(2, kNoEntry);
    if (!fs::is_directory(p.parent_path(), ec)) return finish(2, kNoEntry);
    int fd = -1;
    for (int i = 0; i < 16; ++i)
        if (!g.files[i]) {
            fd = i;
            break;
        }
    if (fd < 0) return finish(2, kTooMany);
    const char *how = exists ? ((mode & kOpenWrite) ? "r+b" : "rb") : "w+b";
    FILE *f = _wfopen(p.wstring().c_str(), std::wstring(how, how + std::strlen(how)).c_str());
    if (!f) return finish(2, kDenied);
    g.files[fd] = f;
    (void)kOpenRead;
    return finish(2, fd);
}

int gt4_replace_sceMcClose(int fd) {
    KernelScope ks;
    if (fd < 0 || fd >= 16 || !g.files[fd]) return finish(3, kNoEntry);
    std::fclose(g.files[fd]);
    g.files[fd] = nullptr;
    return finish(3, 0);
}

int gt4_replace_sceMcSeek(int fd, int offset, int origin) {
    KernelScope ks;
    if (fd < 0 || fd >= 16 || !g.files[fd]) return finish(4, kNoEntry);
    std::fseek(g.files[fd], offset, origin == 1 ? SEEK_CUR : origin == 2 ? SEEK_END : SEEK_SET);
    return finish(4, static_cast<int>(std::ftell(g.files[fd])));
}

int gt4_replace_sceMcRead(int fd, void *buf, int size) {
    KernelScope ks;
    if (g_trace) log_info("memcard: read fd %d %d bytes to %p", fd, size, buf);
    if (fd < 0 || fd >= 16 || !g.files[fd]) return finish(5, kNoEntry);
    return finish(5, static_cast<int>(std::fread(buf, 1, static_cast<size_t>(size), g.files[fd])));
}

int gt4_replace_sceMcWrite(int fd, const void *buf, int size) {
    KernelScope ks;
    if (g_trace) log_info("memcard: write fd %d %d bytes from %p", fd, size, buf);
    if (fd < 0 || fd >= 16 || !g.files[fd]) return finish(6, kNoEntry);
    return finish(6, static_cast<int>(std::fwrite(buf, 1, static_cast<size_t>(size), g.files[fd])));
}

int gt4_replace_sceMcFlush(int fd) {
    KernelScope ks;
    if (fd >= 0 && fd < 16 && g.files[fd]) std::fflush(g.files[fd]);
    return finish(10, 0);
}

int gt4_replace_sceMcMkdir(int port, int, const char *name) {
    KernelScope ks;
    if (g_trace) log_info("memcard: mkdir %s", name);
    if (port != 0) return finish(11, kNoCard);
    const fs::path p = host_path(card_path(name));
    std::error_code ec;
    if (fs::exists(p, ec)) return finish(11, kNoEntry);  // libmc: the entry already exists
    return finish(11, fs::create_directory(p, ec) ? 0 : kNoEntry);
}

int gt4_replace_sceMcChdir(int port, int, const char *name, char *previous) {
    KernelScope ks;
    if (g_trace) log_info("memcard: chdir %s", name);
    if (port != 0) return finish(12, kNoCard);
    if (previous) std::snprintf(previous, 1024, "%s", g.cwd.c_str());
    const std::string card = card_path(name);
    std::error_code ec;
    if (!fs::is_directory(host_path(card), ec)) return finish(12, kNoEntry);
    g.cwd = card;
    return finish(12, 0);
}

int gt4_replace_sceMcGetDir(int port, int, const char *name, unsigned mode, int maxent, void *table) {
    KernelScope ks;
    if (g_trace) log_info("memcard: getdir %s mode %u max %d", name, mode, maxent);
    if (port != 0) return finish(13, kNoCard);
    if (mode == 0) {  // a new listing: name is a directory pattern ("/BASCUS-97328*", "/DIR/*")
        g_listing = {};
        const std::string card = card_path(name);
        const size_t slash = card.rfind('/');
        const std::string dir = card.substr(0, slash ? slash : 1), pattern = card.substr(slash + 1);
        const fs::path hp = host_path(dir);
        std::error_code ec;
        if (!fs::is_directory(hp, ec)) return finish(13, kNoEntry);
        McDirEntry e;
        if (dir != "/" && (pattern == "*" || pattern.empty())) {  // as on a card: "." and ".." first
            fill_entry(e, ".", hp);
            g_listing.entries.push_back(e);
            fill_entry(e, "..", hp.parent_path());
            g_listing.entries.push_back(e);
        }
        for (const auto &it : fs::directory_iterator(hp, ec)) {
            const std::string n = it.path().filename().string();
            if (match(pattern.empty() ? "*" : pattern.c_str(), n.c_str())) {
                fill_entry(e, n, it.path());
                g_listing.entries.push_back(e);
            }
        }
    }
    int n = 0;
    auto *out = static_cast<McDirEntry *>(table);
    while (n < maxent && g_listing.next < g_listing.entries.size()) out[n++] = g_listing.entries[g_listing.next++];
    return finish(13, n);
}

int gt4_replace_sceMcSetFileInfo(int port, int, const char *, const void *, unsigned) {
    return finish(14, port != 0 ? kNoCard : 0);  // times and attributes stay the host's
}

int gt4_replace_sceMcDelete(int port, int, const char *name) {
    KernelScope ks;
    if (port != 0) return finish(15, kNoCard);
    const fs::path p = host_path(card_path(name));
    std::error_code ec;
    if (!fs::exists(p, ec)) return finish(15, kNoEntry);
    if (fs::is_directory(p, ec) && !fs::is_empty(p, ec)) return finish(15, kNotEmpty);
    return finish(15, fs::remove(p, ec) ? 0 : kDenied);
}

int gt4_replace_sceMcFormat(int port, int) { return finish(16, port != 0 ? kNoCard : 0); }
int gt4_replace_sceMcUnformat(int port, int) { return finish(17, port != 0 ? kNoCard : 0); }

int gt4_replace_sceMcGetEntSpace(int port, int, const char *) {
    return finish(18, port != 0 ? kNoCard : 1000);  // plenty of free directory entries
}

int gt4_replace_sceMcRename(int port, int, const char *from, const char *to) {
    KernelScope ks;
    if (port != 0) return finish(19, kNoCard);
    const fs::path a = host_path(card_path(from));
    std::error_code ec;
    if (!fs::exists(a, ec)) return finish(19, kNoEntry);
    fs::rename(a, a.parent_path() / fs::u8path(to), ec);
    return finish(19, ec ? kDenied : 0);
}

int gt4_replace_sceMcSync(int, int *cmd, int *result) {
    if (!g.pending) return -1;  // nothing in progress
    g.pending = false;
    if (cmd) *cmd = g.cmd;
    if (result) *result = g.result;
    return 1;  // finished
}

}  // extern "C"
