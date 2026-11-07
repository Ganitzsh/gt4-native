// The native save folder (docs/SAVES.md) behind libmc: the SDK's sceMc* calls, replaced at their API, on files.
//
// The game's card manager (gt4-decomp unit 005547E8, 00561E00-00562738) issues one libmc call at a
// time and completes it with sceMcSync; each call here does its work at once and leaves the result
// for the next sceMcSync, as an IOP that answered instantly would. The cards are folders
// (runtime/save_folder.h: PS2-style directories as host directories, files as host files, written
// safely), so saves can be copied in and out, and saves from a PS2 come in with gt4_host --import-save.
// What the game sees of a card is what its save code asks: one in the first slot, always present and
// formatted; the second slot is empty unless GT4_SAVES2 names a folder for it.
//
// libmc conventions followed: results 0 success, -1 "new card" (the first GetInfo of a session),
// -4 no such entry, -5 not permitted, -6 directory not empty, -7 too many open files, -10 no card;
// sceMcGetDir entries are 64 bytes (creation and modification time, size, attributes, name); free
// space is counted in 1 KB clusters of an 8 MB card.
#include "guest.h"
#include "kernel.h"
#include "log.h"
#include "runtime.h"
#include "save_folder.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace gt4::rt;
namespace fs = std::filesystem;

namespace {

using namespace gt4::mc;

struct State {
    bool pending = false;
    int cmd = 0, result = 0;
} g;

Cards g_cards;

const bool g_trace = getenv("GT4_TRACE_MC") != nullptr;  // log every call and its result

int finish(int cmd, int result) {
    if (g_trace) log_info("memcard: command %d -> %d", cmd, result);
    g.cmd = cmd;
    g.result = result;
    g.pending = true;
    return 0;
}

void log_line(const char *line) { log_info("%s", line); }

}  // namespace

extern "C" {

int gt4_replace_sceMcInit() {
    // GT4_SAVE_BACKUPS: how many session backups each card keeps (save_folder.h), 10 by default, 0 for none
    const char *b = getenv("GT4_SAVE_BACKUPS");
    const int backups = b && *b ? std::atoi(b) : 10;
    const char *second = getenv("GT4_SAVES2");
    g_cards.setLog(log_line);
    g_cards.insert(0, fs::u8path(g_config.memcard_path), backups);
    g_cards.insert(1, second && *second ? fs::u8path(second) : fs::path(), backups);
    log_info("memcard: slot 1 is %s", g_config.memcard_path);
    if (g_cards.present(1)) log_info("memcard: slot 2 is %s", second);
    return 0;
}
int gt4_replace_sceMcEnd() { return 0; }
int gt4_replace_sceMcChangeThreadPriority(int) { return 0; }
int gt4_replace_sceMcGetSlotMax(int) { return 1; }
int gt4_replace_sceMcUdCheckNewCard() { return 0; }

int gt4_replace_sceMcGetInfo(int port, int, int *type, int *free_kb, int *format) {
    KernelScope ks;
    if (!g_cards.present(port)) {
        if (type) *type = 0;
        if (free_kb) *free_kb = 0;
        if (format) *format = 0;
        return finish(1, kNoCard);
    }
    if (type) *type = 2;  // PS2 memory card
    if (format) *format = 1;
    if (free_kb) *free_kb = g_cards.freeClusters(port);
    return finish(1, g_cards.newCardResult(port));
}

int gt4_replace_sceMcOpen(int port, int, const char *name, int mode) {
    KernelScope ks;
    if (g_trace) log_info("memcard: open %d:%s mode %x", port, name, mode);
    return finish(2, g_cards.open(port, name, mode));
}

int gt4_replace_sceMcClose(int fd) {
    KernelScope ks;
    return finish(3, g_cards.close(fd));
}

int gt4_replace_sceMcSeek(int fd, int offset, int origin) {
    KernelScope ks;
    return finish(4, g_cards.seek(fd, offset, origin));
}

int gt4_replace_sceMcRead(int fd, void *buf, int size) {
    KernelScope ks;
    if (g_trace) log_info("memcard: read fd %d %d bytes to %p", fd, size, buf);
    return finish(5, g_cards.read(fd, buf, size));
}

int gt4_replace_sceMcWrite(int fd, const void *buf, int size) {
    KernelScope ks;
    if (g_trace) log_info("memcard: write fd %d %d bytes from %p", fd, size, buf);
    return finish(6, g_cards.write(fd, buf, size));
}

int gt4_replace_sceMcFlush(int fd) {
    KernelScope ks;
    return finish(10, g_cards.flush(fd));
}

int gt4_replace_sceMcMkdir(int port, int, const char *name) {
    KernelScope ks;
    if (g_trace) log_info("memcard: mkdir %d:%s", port, name);
    return finish(11, g_cards.mkdir(port, name));
}

int gt4_replace_sceMcChdir(int port, int, const char *name, char *previous) {
    KernelScope ks;
    if (g_trace) log_info("memcard: chdir %d:%s", port, name);
    std::string before;
    const int r = g_cards.chdir(port, name, previous ? &before : nullptr);
    if (previous && r != kNoCard) std::snprintf(previous, 1024, "%s", before.c_str());
    return finish(12, r);
}

int gt4_replace_sceMcGetDir(int port, int, const char *name, unsigned mode, int maxent, void *table) {
    KernelScope ks;
    if (g_trace) log_info("memcard: getdir %d:%s mode %u max %d", port, name, mode, maxent);
    return finish(13, g_cards.getdir(port, name, mode, maxent, static_cast<McDirEntry *>(table)));
}

int gt4_replace_sceMcSetFileInfo(int port, int, const char *, const void *, unsigned) {
    return finish(14, g_cards.present(port) ? 0 : kNoCard);  // times and attributes stay the host's
}

int gt4_replace_sceMcDelete(int port, int, const char *name) {
    KernelScope ks;
    return finish(15, g_cards.remove(port, name));
}

int gt4_replace_sceMcFormat(int port, int) { return finish(16, g_cards.present(port) ? 0 : kNoCard); }
int gt4_replace_sceMcUnformat(int port, int) { return finish(17, g_cards.present(port) ? 0 : kNoCard); }

int gt4_replace_sceMcGetEntSpace(int port, int, const char *) {
    return finish(18, g_cards.present(port) ? 1000 : kNoCard);  // plenty of free directory entries
}

int gt4_replace_sceMcRename(int port, int, const char *from, const char *to) {
    KernelScope ks;
    return finish(19, g_cards.rename(port, from, to));
}

int gt4_replace_sceMcSync(int, int *cmd, int *result) {
    if (!g.pending) return -1;  // nothing in progress
    g.pending = false;
    if (cmd) *cmd = g.cmd;
    if (result) *result = g.result;
    return 1;  // finished
}

}  // extern "C"
