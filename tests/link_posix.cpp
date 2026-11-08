// The POSIX game link (platform/gs/link_host.cpp, PLAN.md C9 / R4): the host's GameLink against a fake game in a child
// process, the way the real game comes in through Wine. The test runs itself as the launcher
// (GT4_GAME_LAUNCHER="<this> --fake-game"), so the child gets "--fake-game GAME --link Z:\dev\shm\gt4_link_<pid>",
// maps the link through that Wine path turned back into a Unix one, checks the header, sends kRecords numbered records
// on the game -> host ring (with the game's 8-byte index store), waits for the host's quit command on the
// host -> game ring and exits with kExitCode. The host checks every record, the quit and the exit code.
//   link_posix                      run the test (exit 0: passed)
// Build (standalone, without the host's CMake): define LINK_POSIX_STANDALONE and compile it with link_host.cpp.
#include "link_host.h"
#include "stream.h"   // recordSpan

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

using namespace gt4::ps2;

namespace {

constexpr uint32_t kRecords = 20000;   // 20000 x 8016 bytes, about 160 MB: the 96 MiB game -> host ring wraps
constexpr uint32_t kPayload = 8000;
constexpr int kExitCode = 7;

uint64_t load_index(const volatile uint64_t* p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
void store_index(volatile uint64_t* p, uint64_t v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }

int fakeGame(const char* link) {
    std::string path = link;
    if (path.rfind("Z:", 0) == 0) path = path.substr(2);
    for (char& c : path)
        if (c == '\\') c = '/';
    const int fd = open(path.c_str(), O_RDWR);
    if (fd < 0) {
        std::fprintf(stderr, "fake game: cannot open %s: %s\n", path.c_str(), std::strerror(errno));
        return 2;
    }
    void* view = mmap(nullptr, GT4_LINK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (view == MAP_FAILED) return 3;
    auto* h = static_cast<Gt4LinkHeader*>(view);
    if (h->magic != GT4_LINK_MAGIC || h->version != GT4_LINK_VERSION) return 4;
    uint8_t* g2h = static_cast<uint8_t*>(view) + h->g2h_offset;
    const uint8_t* h2g = static_cast<uint8_t*>(view) + h->h2g_offset;
    h->game_state = 1;
    for (uint32_t i = 0; i < kRecords; ++i) {
        const uint64_t span = recordSpan(kPayload), head = load_index(&h->g2h.head);
        while (head + span - load_index(&h->g2h.tail) > h->g2h_size) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        static uint8_t rec[16 + kPayload];
        const Gt4LinkRecord r{GT4_REC_GFX_DATA, kPayload, i + 1};
        std::memcpy(rec, &r, sizeof r);
        for (uint32_t k = 0; k < kPayload; ++k) rec[16 + k] = static_cast<uint8_t>(i + k);
        for (uint32_t k = 0; k < sizeof rec; ++k) g2h[(head + k) % h->g2h_size] = rec[k];
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        store_index(&h->g2h.head, head + span);
    }
    // the host's quit (GT4_REC_HOST_CMD 1)
    for (int waited = 0; waited < 10000; ++waited) {
        const uint64_t tail = load_index(&h->h2g.tail), head = load_index(&h->h2g.head);
        if (tail + sizeof(Gt4LinkRecord) <= head) {
            Gt4LinkRecord r;
            for (uint32_t k = 0; k < sizeof r; ++k) reinterpret_cast<uint8_t*>(&r)[k] = h2g[(tail + k) % h->h2g_size];
            uint32_t cmd = 0;
            for (uint32_t k = 0; k < 4; ++k) reinterpret_cast<uint8_t*>(&cmd)[k] = h2g[(tail + sizeof r + k) % h->h2g_size];
            store_index(&h->h2g.tail, tail + recordSpan(r.size));
            if (r.type == GT4_REC_HOST_CMD && cmd == 1) return kExitCode;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return 5;   // no quit within 10 s
}

}  // namespace

#ifdef LINK_POSIX_STANDALONE
namespace gt4::ps2 {   // (stream.cpp's, which the standalone build does not compile)
size_t splitRecords(const uint8_t* data, size_t size, const std::function<void(const Gt4LinkRecord&, const uint8_t*)>& onRecord) {
    size_t offset = 0;
    while (offset + sizeof(Gt4LinkRecord) <= size) {
        Gt4LinkRecord header;
        std::memcpy(&header, data + offset, sizeof header);
        const size_t span = recordSpan(header.size);
        if (offset + span > size) break;
        onRecord(header, data + offset + sizeof header);
        offset += span;
    }
    return offset;
}
}  // namespace gt4::ps2
#endif

int main(int argc, char** argv) {
    if (argc >= 5 && !std::strcmp(argv[1], "--fake-game") && !std::strcmp(argv[3], "--link")) return fakeGame(argv[4]);
    char self[4096] = {};
    if (readlink("/proc/self/exe", self, sizeof self - 1) <= 0) return 10;
    setenv("GT4_GAME_LAUNCHER", (std::string(self) + " --fake-game").c_str(), 1);
    unsetenv("GT4_LINK_FILE");
    GameLink::Options options;
    options.gamePath = "gt4_game.exe";
    std::string error;
    auto link = GameLink::start(options, error);
    if (!link) {
        std::fprintf(stderr, "start: %s\n", error.c_str());
        return 11;
    }
    const std::string shmFile = "/dev/shm" + link->name();
    uint32_t got = 0, bad = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (got < kRecords && std::chrono::steady_clock::now() < deadline) {
        link->poll([&](const Gt4LinkRecord& r, const uint8_t* p) {
            bool ok = r.type == GT4_REC_GFX_DATA && r.size == kPayload && r.seq == got + 1;
            for (uint32_t k = 0; ok && k < kPayload; ++k) ok = p[k] == static_cast<uint8_t>(got + k);
            bad += !ok;
            ++got;
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const bool fileThere = access(shmFile.c_str(), F_OK) == 0;
    link.reset();   // sends the quit, waits for the game
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const bool fileGone = access(shmFile.c_str(), F_OK) != 0;
    std::printf("link_posix: %u of %u records, %u bad; link file %s while running, %s after; ", got, kRecords, bad,
                fileThere ? "present" : "MISSING", fileGone ? "removed" : "LEFT BEHIND");
    // (the exit code is read from a second link object's monitor: re-run one game to check it)
    setenv("GT4_GAME_LAUNCHER", (std::string(self) + " --fake-game").c_str(), 1);
    auto second = GameLink::start(options, error);
    if (!second) return 12;
    uint32_t n = 0;
    while (n < kRecords) {
        second->poll([&](const Gt4LinkRecord&, const uint8_t*) { ++n; });
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const uint32_t quit = 1;
    second->write(GT4_REC_HOST_CMD, &quit, sizeof quit);
    for (int i = 0; i < 3000 && second->running(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const int code = second->exitCode();
    std::printf("the game's exit code %d (expected %d)\n", code, kExitCode);
    const bool passed = got == kRecords && bad == 0 && fileThere && fileGone && !second->running() && code == kExitCode;
    std::printf("%s\n", passed ? "PASSED" : "FAILED");
    return passed ? 0 : 1;
}
