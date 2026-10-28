// GT4_PROFILE=FILE[:FROM[:TO]]: a sampling profiler of the game's native code. A thread samples every
// thread of the process each millisecond (from VBlank FROM to VBlank TO when given): it suspends the
// thread, reads its instruction pointer and walks its frame-pointer chain (game code keeps frame
// pointers), then counts each function on the chain once (inclusive), the innermost one as self, and
// every caller -> callee edge. Only samples taken in game code count (time in the runtime's own
// functions, memcpy and the like, is left out with the waiting threads). FILE is rewritten every two seconds: functions by inclusive samples, then
// the callees of the busiest ones, to find which functions a scene spends its time in.
#include "image.h"
#include "kernel.h"
#include "log.h"
#include "runtime.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Stats {
    uint32_t guest = 0;
    uint64_t self = 0, total = 0;
};

std::string g_path;
uint32_t g_from = 0, g_to = 0xFFFFFFFFu;
std::map<const char *, Stats> g_stats;
std::map<std::pair<const char *, const char *>, uint64_t> g_edges;
uint64_t g_samples = 0;

constexpr int kMaxDepth = 96;

// The chain of one sample: host code addresses, innermost first.
int walk(HANDLE h, uint32_t *out) {
    CONTEXT ctx;
    ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (!GetThreadContext(h, &ctx)) return 0;
    int n = 0;
    out[n++] = ctx.Eip;
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery(reinterpret_cast<void *>(static_cast<uintptr_t>(ctx.Esp)), &mi, sizeof mi) || mi.State != MEM_COMMIT)
        return n;
    const uint32_t lo = ctx.Esp, hi = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mi.BaseAddress) + mi.RegionSize);
    uint32_t bp = ctx.Ebp;
    while (n < kMaxDepth && bp >= lo && bp + 8 <= hi && !(bp & 3)) {
        const auto *f = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(bp));
        out[n++] = f[1];
        if (f[0] <= bp) break;
        bp = f[0];
    }
    return n;
}

void account(const uint32_t *chain, int n) {
    // only threads running game code: the others wait in the runtime or the system
    uint32_t g0 = 0;
    if (!n || !*gt4::rt::native_name_for(chain[0], &g0) || !g0) return;
    const char *names[kMaxDepth];
    int m = 0;
    for (int k = 0; k < n; ++k) {
        uint32_t g = 0;
        const char *name = gt4::rt::native_name_for(chain[k], &g);
        if (!*name) continue;
        names[m++] = name;
        Stats &s = g_stats[name];
        s.guest = g;
    }
    if (!m) return;
    ++g_samples;
    g_stats[names[0]].self++;
    std::set<const char *> seen;
    std::set<std::pair<const char *, const char *>> edges;
    for (int k = 0; k < m; ++k) {
        if (seen.insert(names[k]).second) g_stats[names[k]].total++;
        if (k + 1 < m) edges.insert({names[k + 1], names[k]});
    }
    for (const auto &e : edges) g_edges[e]++;
}

void write_report() {
    std::vector<std::pair<const char *, Stats>> rows(g_stats.begin(), g_stats.end());
    std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.second.total > b.second.total; });
    FILE *f = fopen(g_path.c_str(), "w");
    if (!f) return;
    fprintf(f, "# %llu samples (VBlank %u..%u); inclusive%%, self%%, PS2 address, native function\n",
            static_cast<unsigned long long>(g_samples), g_from, g_to);
    const double scale = g_samples ? 100.0 / static_cast<double>(g_samples) : 0;
    for (size_t k = 0; k < rows.size() && k < 400; ++k)
        fprintf(f, "%6.2f %6.2f %08x %s\n", rows[k].second.total * scale, rows[k].second.self * scale, rows[k].second.guest,
                rows[k].first);
    fprintf(f, "\n# callees of the 150 busiest functions (inclusive%% of all samples)\n");
    for (size_t k = 0; k < rows.size() && k < 150; ++k) {
        std::vector<std::pair<uint64_t, const char *>> callees;
        for (const auto &e : g_edges)
            if (e.first.first == rows[k].first) callees.push_back({e.second, e.first.second});
        if (callees.empty()) continue;
        std::sort(callees.rbegin(), callees.rend());
        fprintf(f, "%s (%08x)\n", rows[k].first, rows[k].second.guest);
        for (size_t c = 0; c < callees.size() && c < 24; ++c)
            if (callees[c].first * scale >= 0.05)
                fprintf(f, "    %6.2f %08x %s\n", callees[c].first * scale, g_stats[callees[c].second].guest, callees[c].second);
    }
    fclose(f);
}

DWORD WINAPI profile_thread(void *) {
    const DWORD self = GetCurrentThreadId();
    std::map<DWORD, HANDLE> threads;
    DWORD last_scan = 0, last_write = GetTickCount();
    uint32_t chain[kMaxDepth];
    // a high-resolution timer: Sleep(1) waits a whole 15.6 ms scheduler tick
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
    for (;;) {
        LARGE_INTEGER due;
        due.QuadPart = -10000;  // 1 ms
        if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE))
            WaitForSingleObject(timer, INFINITE);
        else
            Sleep(1);
        const uint32_t vb = gt4::rt::kernel_vblank_count();
        if (vb < g_from) continue;
        if (vb >= g_to) {
            write_report();
            gt4::rt::log_info("profile: %llu samples written to %s", static_cast<unsigned long long>(g_samples), g_path.c_str());
            return 0;
        }
        const DWORD now = GetTickCount();
        if (now - last_scan > 500) {  // threads come and go: rescan twice a second
            last_scan = now;
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            THREADENTRY32 te;
            te.dwSize = sizeof te;
            std::set<DWORD> alive;
            for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
                if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == self) continue;
                alive.insert(te.th32ThreadID);
                if (!threads.count(te.th32ThreadID))
                    if (HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, te.th32ThreadID))
                        threads[te.th32ThreadID] = h;
            }
            CloseHandle(snap);
            for (auto it = threads.begin(); it != threads.end();)
                if (!alive.count(it->first)) {
                    CloseHandle(it->second);
                    it = threads.erase(it);
                } else {
                    ++it;
                }
        }
        for (const auto &t : threads) {
            // Nothing that can take a lock between suspend and resume: the thread may hold the heap's.
            if (SuspendThread(t.second) == static_cast<DWORD>(-1)) continue;
            const int n = walk(t.second, chain);
            ResumeThread(t.second);
            account(chain, n);
        }
        if (now - last_write > 2000) {
            last_write = now;
            write_report();
        }
    }
}

}  // namespace

void gt4::rt::profile_start() {
    const char *p = getenv("GT4_PROFILE");
    if (!p || !*p) return;
    std::string spec = p;
    // FILE[:FROM[:TO]]; a drive letter's colon is part of FILE
    std::vector<std::string> parts;
    size_t at = 0;
    for (size_t k = 0; k <= spec.size(); ++k)
        if (k == spec.size() || (spec[k] == ':' && k != 1)) {
            parts.push_back(spec.substr(at, k - at));
            at = k + 1;
        }
    g_path = parts[0];
    if (parts.size() > 1) g_from = static_cast<uint32_t>(strtoul(parts[1].c_str(), nullptr, 0));
    if (parts.size() > 2) g_to = static_cast<uint32_t>(strtoul(parts[2].c_str(), nullptr, 0));
    CreateThread(nullptr, 1u << 20, profile_thread, nullptr, 0, nullptr);
}
