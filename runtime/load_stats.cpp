// GT4_LOAD_STATS=1 (measurement, PLAN.md D10): the game's loads, one log line per load. A load is a run of VBlanks with
// disc reads in it, ended by a second (60 VBlanks) without one. Each line gives the VBlanks it spans and its wall time,
// then where that time went:
//   disc    the time inside the disc reads (iso_read: a synchronous ReadFile loop, on the game's thread), the bytes
//           and the reads;
//   CPU     the game process's CPU time over the load (all its threads: inflating GT4.VOL's files, parsing, the
//           frames it still draws, audio);
//   frames  the frames the game drew (frame kicks) against the VBlanks: a loading screen paced by whole frames draws
//           few while the wall time runs;
//   blocked the VBlanks without a new frame (VBlanks less frames drawn) and their time at 59.94 Hz: the game kept
//           from its frame (a waited read, a lock). PLAN D10 (c)'s target: the race load's under 0.2 s. It assumes
//           a screen drawn every VBlank: on one GT4 paces at 30 fps, half its VBlanks count with nothing blocking.
// What is neither disc nor CPU is waiting: the frame clock (the loading frames GT4 draws at the VBlank rate count
// here too, so a load that draws every frame still shows some), a lock, the host. Off without the variable: nothing
// is timed, nothing logged.
#include "load_stats.h"

#include "log.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>

namespace gt4::rt {

namespace {

bool enabled() {
    static const bool on = [] {
        const char *e = std::getenv("GT4_LOAD_STATS");
        return e && *e && *e != '0';
    }();
    return on;
}

double seconds_now() {
    static const double frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) / frequency;
}

double process_cpu_seconds() {
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0.0;
    const auto add = [](const FILETIME &f) { return (uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    return static_cast<double>(add(kernel) + add(user)) * 1e-7;
}

// the disc reads so far (any thread), in microseconds and bytes
std::atomic<uint64_t> g_io_us{0}, g_io_bytes{0}, g_io_reads{0}, g_kicks{0};

// the counters at a VBlank, and the load being measured from such a point (the VBlank hook's thread only)
struct Load {
    bool open = false;
    uint32_t first = 0, lastIo = 0;
    double wall = 0.0, cpu = 0.0;
    uint64_t ioUs = 0, ioBytes = 0, ioReads = 0, kicks = 0;
} g_load, g_before, g_end;   // g_before: the previous VBlank's counters; g_end: the load's last read's VBlank

}  // namespace

double load_stats_begin() { return enabled() ? seconds_now() : 0.0; }

void load_stats_read(double started, uint32_t bytes) {
    if (!enabled()) return;
    g_io_us += static_cast<uint64_t>((seconds_now() - started) * 1e6);
    g_io_bytes += bytes;
    ++g_io_reads;
}

void load_stats_kick() {
    if (enabled()) ++g_kicks;
}

void load_stats_vblank(uint32_t vblank) {
    if (!enabled()) return;
    const uint64_t reads = g_io_reads.load();
    const bool read = reads != g_before.ioReads;
    const Load now{false, vblank, vblank, seconds_now(), process_cpu_seconds(), g_io_us.load(), g_io_bytes.load(), reads,
                   g_kicks.load()};
    if (read && !g_load.open) {   // a load starts: from the previous VBlank's counters (its first reads count)
        g_load = g_before;
        g_load.open = true;
        g_load.first = vblank;
    }
    if (read && g_load.open) g_end = now;   // its end so far: this VBlank
    g_before = now;
    if (!g_load.open || read || vblank - g_end.first < 60) return;
    // a second without a read: the load ended at its last read's VBlank (the second after it is not counted)
    g_load.open = false;
    const Load &l = g_load, &e = g_end;
    const double wall = e.wall - l.wall, cpu = e.cpu - l.cpu, io = static_cast<double>(e.ioUs - l.ioUs) * 1e-6;
    const double mb = static_cast<double>(e.ioBytes - l.ioBytes) / (1 << 20);
    const uint32_t vblanks = e.first - l.first + 1;
    const uint64_t frames = e.kicks - l.kicks;
    const uint64_t blocked = frames < vblanks ? vblanks - frames : 0;
    log_info("load: VBlanks %u-%u (%u, %.2f s): disc %.2f s (%.1f MiB in %llu reads, %.0f MiB/s), CPU %.2f s, "
             "frames drawn %llu, blocked %llu (%.2f s), waiting %.2f s",
             l.first, e.first, vblanks, wall, io, mb, static_cast<unsigned long long>(e.ioReads - l.ioReads),
             io > 0 ? mb / io : 0.0, cpu, static_cast<unsigned long long>(frames),
             static_cast<unsigned long long>(blocked), static_cast<double>(blocked) / 59.94,
             wall - io - cpu > 0.0 ? wall - io - cpu : 0.0);   // (CPU of several threads can exceed the wall time)
}

}  // namespace gt4::rt
