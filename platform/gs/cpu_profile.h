#pragma once
// GT4_CPU_PROFILE=1 (performance work): the time the host's threads spend in labelled scopes, printed per frame at
// exit (cpuprofile::print). A scope: GT4_CPU_SCOPE("label"); nested scopes each count their own whole time. Off,
// a scope costs a test of one flag.
// GT4_STAGE_SCOPE("label"): a coarse stage of a thread's loop (a few a frame), measured whenever the hitch log is on
// (GT4_HITCH_LOG, default on) too: a long hitch names the stages that took the time (cpuprofile::busiest), the
// one still running included.
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

// The coarse stages (GT4_STAGE_SCOPE): with the CPU profile, or for the hitch log.
inline bool stagesMeasured() {
    static const bool on = [] {
        const char* e = std::getenv("GT4_HITCH_LOG");
        return enabled() || !(e && *e == '0');
    }();
    return on;
}

inline int64_t nowNanoseconds() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct Slot {
    const char* label;
    std::atomic<uint64_t> nanoseconds{0}, calls{0};
    std::atomic<uint64_t> longest{0};     // the longest call since busiest() last looked
    std::atomic<int64_t> openSince{0};    // a call running now (a stage runs on one thread): when it began
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
        if (!s) return;
        start = nowNanoseconds();
        s->openSince.store(start, std::memory_order_relaxed);
    }
    ~Scope() {
        if (!s) return;
        const uint64_t ns = static_cast<uint64_t>(nowNanoseconds() - start);
        s->openSince.store(0, std::memory_order_relaxed);
        s->nanoseconds += ns;
        ++s->calls;
        uint64_t was = s->longest.load(std::memory_order_relaxed);
        while (ns > was && !s->longest.compare_exchange_weak(was, ns, std::memory_order_relaxed)) {}
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Slot* s;
    int64_t start = 0;
};

// Each scope's time since the last call (the caller keeps `last`, indexed as the registry), for the scopes that ran:
// a frame's stages for gt4_host --profile-json (GT4_STAGE_SCOPE; every GT4_CPU_SCOPE with GT4_CPU_PROFILE=1).
template <typename Out>
inline void since(std::vector<uint64_t>& last, Out&& out) {
    std::lock_guard<std::mutex> lock(registryMutex());
    auto& slots = registry();
    last.resize(slots.size(), 0);
    for (size_t i = 0; i < slots.size(); ++i) {
        const uint64_t now = slots[i]->nanoseconds.load(std::memory_order_relaxed);
        if (now != last[i]) out(slots[i]->label, now - last[i]);
        last[i] = now;
    }
}

// The stages that took the most time since the last call (the caller keeps `last`), as "label 812.4 ms (3 calls,
// longest 790.1 ms)", those of at least minMs, at most `count`; a call still running counts up to now, marked "running".
inline std::string busiest(std::vector<uint64_t>& last, double minMs, size_t count) {
    struct Row {
        const Slot* s;
        double ms, longest;
        uint64_t calls;
        bool running;
    };
    std::vector<Row> rows;
    const int64_t now = nowNanoseconds();
    {
        std::lock_guard<std::mutex> lock(registryMutex());
        auto& slots = registry();
        last.resize(slots.size() * 2, 0);
        for (size_t i = 0; i < slots.size(); ++i) {
            const Slot& s = *slots[i];
            const uint64_t ns = s.nanoseconds.load(), calls = s.calls.load();
            const int64_t open = s.openSince.load(std::memory_order_relaxed);
            const double running = open ? double(now - open) * 1e-6 : 0.0;
            const double ms = double(ns - last[i * 2]) * 1e-6 + running;
            const double longest = std::max(double(slots[i]->longest.exchange(0)) * 1e-6, running);
            if (ms >= minMs) rows.push_back({&s, ms, longest, calls - last[i * 2 + 1], open != 0});
            last[i * 2] = ns, last[i * 2 + 1] = calls;
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.ms > b.ms; });
    std::string out;
    char text[160];
    for (size_t i = 0; i < rows.size() && i < count; ++i) {
        std::snprintf(text, sizeof text, "%s%s %.1f ms (%llu calls, longest %.1f ms%s)", out.empty() ? "" : ", ", rows[i].s->label,
                      rows[i].ms, static_cast<unsigned long long>(rows[i].calls), rows[i].longest, rows[i].running ? ", running" : "");
        out += text;
    }
    return out;
}

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
#define GT4_STAGE_SCOPE(label)                                                                                        \
    static ::gt4::cpuprofile::Slot& GT4_CPU_SCOPE_JOIN(gt4StageSlot, __LINE__) = ::gt4::cpuprofile::slot(label);    \
    ::gt4::cpuprofile::Scope GT4_CPU_SCOPE_JOIN(gt4StageScope, __LINE__)(                                             \
        ::gt4::cpuprofile::stagesMeasured() ? &GT4_CPU_SCOPE_JOIN(gt4StageSlot, __LINE__) : nullptr)
