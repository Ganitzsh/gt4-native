#pragma once
// GT4_CPU_PROFILE=1 (performance work): the time the host's threads spend in labelled scopes, printed per frame at
// exit (cpuprofile::print). A scope: GT4_CPU_SCOPE("label"); nested scopes each count their own whole time. Off,
// a scope costs a test of one flag.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace gt4::cpuprofile {

inline bool enabled() {
    static const bool on = [] {
        const char* e = std::getenv("GT4_CPU_PROFILE");
        return e && *e && *e != '0';
    }();
    return on;
}

struct Slot {
    const char* label;
    std::atomic<uint64_t> nanoseconds{0}, calls{0};
};

inline std::mutex& registryMutex() {
    static std::mutex m;
    return m;
}
inline std::vector<std::unique_ptr<Slot>>& registry() {
    static std::vector<std::unique_ptr<Slot>> slots;
    return slots;
}
inline Slot& slot(const char* label) {
    std::lock_guard<std::mutex> lock(registryMutex());
    for (auto& s : registry())
        if (std::string(s->label) == label) return *s;
    registry().push_back(std::make_unique<Slot>());
    registry().back()->label = label;
    return *registry().back();
}

class Scope {
public:
    explicit Scope(Slot* s) : s(s) {
        if (s) start = std::chrono::steady_clock::now();
    }
    ~Scope() {
        if (!s) return;
        s->nanoseconds += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
        ++s->calls;
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Slot* s;
    std::chrono::steady_clock::time_point start;
};

// From now on (GT4_CPU_FROM: a race, not the boot before it).
inline void reset() {
    std::lock_guard<std::mutex> lock(registryMutex());
    for (auto& s : registry()) {
        s->nanoseconds = 0;
        s->calls = 0;
    }
}

// Every scope's time a frame (milliseconds) and calls a frame, the largest first.
inline void print(uint64_t frames) {
    if (!enabled() || !frames) return;
    std::lock_guard<std::mutex> lock(registryMutex());
    std::vector<Slot*> rows;
    for (auto& s : registry()) rows.push_back(s.get());
    std::sort(rows.begin(), rows.end(), [](const Slot* a, const Slot* b) { return a->nanoseconds > b->nanoseconds; });
    std::printf("CPU profile, %llu frames (ms a frame, calls a frame):\n", static_cast<unsigned long long>(frames));
    for (const Slot* s : rows)
        std::printf("  %-34s %8.3f ms %10.1f\n", s->label, double(s->nanoseconds) * 1e-6 / double(frames), double(s->calls) / double(frames));
}

}  // namespace gt4::cpuprofile

#define GT4_CPU_SCOPE_JOIN2(a, b) a##b
#define GT4_CPU_SCOPE_JOIN(a, b) GT4_CPU_SCOPE_JOIN2(a, b)
#define GT4_CPU_SCOPE(label)                                                                                          \
    static ::gt4::cpuprofile::Slot& GT4_CPU_SCOPE_JOIN(gt4CpuSlot, __LINE__) = ::gt4::cpuprofile::slot(label);      \
    ::gt4::cpuprofile::Scope GT4_CPU_SCOPE_JOIN(gt4CpuScope, __LINE__)(::gt4::cpuprofile::enabled() ? &GT4_CPU_SCOPE_JOIN(gt4CpuSlot, __LINE__) : nullptr)
