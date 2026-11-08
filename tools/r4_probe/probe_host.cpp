// R4 probe, host side (probe.h): makes the file, starts probe_game.exe, plays the host's part, reports.
//   Linux:   probe_host [LAUNCHER WORDS...] -- PROBE_GAME.EXE     the game through e.g. "wine" or
//            "FEXInterpreter /usr/bin/wine"; the file is /dev/shm/gt4_probe_<pid>, given to the game as Z:\dev\shm\...
//   Windows: probe_host -- PROBE_GAME.EXE                         (a local check: the game started directly)
// Exit 0: both sides got every record intact, across the 2^32 wrap of the indices.
// Build: zig c++ -target aarch64-linux-gnu.2.35 -O2 -std=c++20 probe_host.cpp -o probe_host (or x86_64-linux-gnu.2.35)
#include "probe.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

int main(int argc, char** argv) {
    std::vector<std::string> launcher;
    std::string game;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--") && i + 1 < argc) {
            game = argv[i + 1];
            break;
        }
        launcher.push_back(argv[i]);
    }
    if (game.empty()) {
        std::fprintf(stderr, "usage: probe_host [LAUNCHER WORDS...] -- PROBE_GAME.EXE\n");
        return 2;
    }
#ifdef _WIN32
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    const std::string path = std::string(tmp) + "gt4_probe_" + std::to_string(GetCurrentProcessId());
    HANDLE f = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    HANDLE m = f != INVALID_HANDLE_VALUE ? CreateFileMappingA(f, nullptr, PAGE_READWRITE, 0, probe::kFileBytes, nullptr) : nullptr;
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    void* view = m ? MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, probe::kFileBytes) : nullptr;
    const std::string gamePath = path;
#else
    const std::string name = "/gt4_probe_" + std::to_string(getpid());
    const std::string path = "/dev/shm" + name;
    const int fd = open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0600);
    void* view = fd >= 0 && ftruncate(fd, probe::kFileBytes) == 0
                     ? mmap(nullptr, probe::kFileBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) : MAP_FAILED;
    if (view == MAP_FAILED) view = nullptr;
    std::string gamePath = "Z:" + path;
    for (char& c : gamePath)
        if (c == '/') c = '\\';
#endif
    if (!view) {
        std::fprintf(stderr, "probe_host: cannot make %s\n", path.c_str());
        return 3;
    }
    auto* h = static_cast<probe::Header*>(view);
    std::memset(view, 0, 4096);
    h->records = probe::kRecords;
    h->g2h.head = probe::kStart;
    h->g2h.tail = probe::kStart;
    h->h2g.head = probe::kStart;
    h->h2g.tail = probe::kStart;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    h->magic = probe::kMagic;
    uint8_t* base = static_cast<uint8_t*>(view);

    // the game side
#ifdef _WIN32
    std::string line = "\"" + game + "\" --link \"" + gamePath + "\"";
    STARTUPINFOA si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(game.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        std::fprintf(stderr, "probe_host: cannot start %s\n", game.c_str());
        return 4;
    }
#else
    std::vector<std::string> words = launcher;
    words.push_back(game);
    words.push_back("--link");
    words.push_back(gamePath);
    std::vector<char*> args;
    for (auto& w : words) args.push_back(w.data());
    args.push_back(nullptr);
    pid_t pid = -1;
    if (posix_spawnp(&pid, args[0], nullptr, nullptr, args.data(), environ) != 0) {
        std::fprintf(stderr, "probe_host: cannot start %s\n", args[0]);
        return 4;
    }
#endif
    std::printf("probe_host: %s, the game given %s; %u records of %u bytes each way, indices from %llu\n", path.c_str(),
                gamePath.c_str(), probe::kRecords, probe::kRecordBytes, static_cast<unsigned long long>(probe::kStart));
    std::fflush(stdout);
    const auto t0 = std::chrono::steady_clock::now();
    uint32_t bad = 0, anomalies = 0;
    const uint32_t good = probe::exchange(h->h2g, base + 4096 + probe::kRingBytes, h->g2h, base + 4096,
                                          [] { __atomic_thread_fence(__ATOMIC_SEQ_CST); }, &bad, &anomalies);
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    int gameExit = -1;
#ifdef _WIN32
    WaitForSingleObject(pi.hProcess, 30000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    gameExit = static_cast<int>(code);
    UnmapViewOfFile(view);
    CloseHandle(m);
    DeleteFileA(path.c_str());
#else
    int status = 0;
    waitpid(pid, &status, 0);
    gameExit = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    munmap(view, probe::kFileBytes);
    close(fd);
    unlink(path.c_str());
#endif
    std::printf("probe_host: received %u of %u records intact, %u bad, %u impossible indices, in %lld ms; the game's side "
                "exited %d\n", good, probe::kRecords, bad, anomalies, ms, gameExit);
    const bool passed = good == probe::kRecords && bad == 0 && anomalies == 0 && gameExit == 0;
    std::printf("%s\n", passed ? "PROBE PASSED" : "PROBE FAILED");
    return passed ? 0 : 1;
}
