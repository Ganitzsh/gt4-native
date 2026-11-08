// R4 probe, game side: i686 Windows, run by Wine (probe.h). probe_game.exe --link PATH (a Windows path to the file
// probe_host made, Z:\dev\shm\... under Wine). Exit 0 when every record came back intact, 1 otherwise, 2 or more when
// it could not map the file.
// Build: i686-w64-mingw32-g++ -O2 -msse2 -std=c++20 probe_game.cpp -o probe_game.exe -static
#include "probe.h"

#include <cstdio>
#include <cstring>
#include <windows.h>

int main(int argc, char** argv) {
    if (argc < 3 || std::strcmp(argv[1], "--link")) {
        std::fprintf(stderr, "usage: probe_game --link PATH\n");
        return 2;
    }
    HANDLE f = CreateFileA(argv[2], GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "probe_game: cannot open %s (Windows error %lu)\n", argv[2], GetLastError());
        return 3;
    }
    HANDLE m = CreateFileMappingA(f, nullptr, PAGE_READWRITE, 0, probe::kFileBytes, nullptr);
    CloseHandle(f);
    void* view = m ? MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, probe::kFileBytes) : nullptr;
    if (!view) {
        std::fprintf(stderr, "probe_game: cannot map %s (Windows error %lu)\n", argv[2], GetLastError());
        return 4;
    }
    auto* h = static_cast<probe::Header*>(view);
    if (h->magic != probe::kMagic) {
        std::fprintf(stderr, "probe_game: not a probe file\n");
        return 5;
    }
    uint8_t* base = static_cast<uint8_t*>(view);
    uint32_t bad = 0, anomalies = 0;
    const DWORD t0 = GetTickCount();
    const uint32_t good = probe::exchange(h->g2h, base + 4096, h->h2g, base + 4096 + probe::kRingBytes,
                                          [] { MemoryBarrier(); }, &bad, &anomalies);
    h->gameDone = good;
    std::printf("probe_game%s: received %u of %u records intact, %u bad, %u impossible indices, in %lu ms; its ring "
                "index ended at %llu (low word wrapped: %s)\n",
#ifdef PROBE_TORN
                " (CONTROL, split 32-bit indices)",
#else
                "",
#endif
                good, probe::kRecords, bad, anomalies, GetTickCount() - t0,
                static_cast<unsigned long long>(probe::load_index(&h->h2g.tail)),
                probe::load_index(&h->h2g.tail) > (1ull << 32) ? "yes" : "no");
    UnmapViewOfFile(view);
    CloseHandle(m);
    return good == probe::kRecords && bad == 0 && anomalies == 0 ? 0 : 1;
}
