// Differential tests of lane M's game_src copies that call through the race's vtable (MENU_DESIGN.md M9d2; lane U's
// design, 2026-11-09). See difftest.h.
//
// 0038B0E0, RaceBasic::updatePlayCheckPause (game_src/func_0038B0E0.cpp): with no command 6 waiting the copy must be
// GT4's function in every branch. Each test is one of the copy's race states (and visual mode), on a race object in
// scratch whose vtable is RaceGTmode's (0x684678). The five slots it calls, 70 (the RaceInput), 152 (the auto demo's
// exit), 23 (the replay's), 87 (terminate) and 150 (pauseStart), are pointed at recorders for the test: each slot's
// PS2 function gets a jmp to a recorder (its 5 bytes saved, restored by the teardown), so the native copy reaches it
// through its trampoline and the original through orig_bridge's hook, both the same native function. The recorders
// write which slot ran and its integer arguments into scratch, read the predicates' answers and the RaceInput's
// address from scratch (no rnd(): the two sides would draw different numbers), and run() compares that like any
// memory. terminate's two floats (always 0.5) are not recorded: 0038AAB0 has no kinds.txt entry, so the original
// passes integers. RaceInput__getButtonDown runs for real on both sides, from a RaceInput in scratch with random down
// words. The teardown logs, per state, how often each branch ran, so a state that never reaches its branch shows.
#include "difftest.h"

#include "log.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <initializer_list>

using namespace difftest;

namespace {

constexpr std::uint32_t kObj = kScratch + 0x10000;     // the race object (fields up to +0xE408)
constexpr std::uint32_t kOrg = kScratch + 0x30000;     // its race organisation (*(race + 0x6C))
constexpr std::uint32_t kInput = kScratch + 0x31000;   // the RaceInput slot 70 returns
constexpr std::uint32_t kRec = kScratch + 0x32000;     // the recorders' log: a count, then (slot, a0, a1) entries
constexpr std::uint32_t kAns = kScratch + 0x33000;     // the predicates' answers: 152, then 23
constexpr std::uint32_t kPauseRace = kScratch + 0x34000, kPauseSimple = kScratch + 0x34100;
constexpr std::uint32_t kVtable = 0x00684678;          // RaceGTmode's vtable (entries of 8 bytes, the function at +4)
constexpr int kMaxRec = 16;

void put32(std::uint32_t at, std::uint32_t v) { std::memcpy(ram(at), &v, 4); }
std::uint32_t get32(std::uint32_t at) {
    std::uint32_t v;
    std::memcpy(&v, ram(at), 4);
    return v;
}
void putf(std::uint32_t at, float f) { std::memcpy(ram(at), &f, 4); }

// Branch tallies (both sides count: halved when logged), per state.
int g_state = 0;
long g_count[6][5] = {};   // pauseStart, terminate(1), terminate(2), the SimplePause, nothing
enum { kPause, kTerm1, kTerm2, kSimple };

void log_call(std::uint32_t slot, std::uint32_t a0, std::uint32_t a1) {
    const std::uint32_t n = get32(kRec);
    if (n < kMaxRec) {
        put32(kRec + 4 + 12 * n, slot);
        put32(kRec + 8 + 12 * n, a0);
        put32(kRec + 12 + 12 * n, a1);
    }
    put32(kRec, n + 1);
}

std::uint32_t rec_input(std::uint32_t self) {
    log_call(70, self, 0);
    return kInput;
}
std::int32_t rec_demo(std::uint32_t self) {
    log_call(152, self, 0);
    return static_cast<std::int32_t>(get32(kAns));
}
std::int32_t rec_replay(std::uint32_t self) {
    log_call(23, self, 0);
    return static_cast<std::int32_t>(get32(kAns + 4));
}
void rec_terminate(std::uint32_t self, std::int32_t how) {
    log_call(87, self, static_cast<std::uint32_t>(how));
    ++g_count[g_state][how == 1 ? kTerm1 : kTerm2];
}
void rec_pause(std::uint32_t self, std::uint32_t pause) {
    log_call(150, self, pause);
    ++g_count[g_state][pause == kPauseRace ? kPause : kSimple];
}

struct Patch {
    int slot;
    const void *recorder;
    std::uint32_t addr = 0;
    std::uint8_t saved[5] = {};
};
Patch g_patches[] = {
    {70, reinterpret_cast<const void *>(&rec_input)},
    {152, reinterpret_cast<const void *>(&rec_demo)},
    {23, reinterpret_cast<const void *>(&rec_replay)},
    {87, reinterpret_cast<const void *>(&rec_terminate)},
    {150, reinterpret_cast<const void *>(&rec_pause)},
};
bool g_patched = false;

void patch() {
    if (g_patched) return;
    for (Patch &p : g_patches) {
        p.addr = get32(kVtable + 8 * p.slot + 4);
        std::uint8_t *at = ram(p.addr);
        std::memcpy(p.saved, at, 5);
        const std::int32_t rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(p.recorder) -
                                                            (reinterpret_cast<std::uintptr_t>(at) + 5));
        at[0] = 0xE9;
        std::memcpy(at + 1, &rel, 4);
    }
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    g_patched = true;
}

void teardown() {
    if (!g_patched) return;
    for (Patch &p : g_patches) std::memcpy(ram(p.addr), p.saved, 5);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    g_patched = false;
    const long *c = g_count[g_state];
    // (log_info, as run() logs: the tally reaches --log FILE too, lane U)
    gt4::rt::log_info("difftest 0038B0E0.s%d: branches (each side): pauseStart %ld, terminate(1) %ld, terminate(2) %ld, "
                      "SimplePause %ld", g_state, c[kPause] / 2, c[kTerm1] / 2, c[kTerm2] / 2, c[kSimple] / 2);
}

// One trial: the race object and its RaceInput for `state` (the copy's gt4_race_pause_command states, 5 = visual).
void setup_state(int state, std::uint32_t args[8]) {
    patch();
    g_state = state;
    std::memset(ram(kObj), 0, 0xE410);
    std::memset(ram(kOrg), 0, 0x100);
    std::memset(ram(kInput), 0, 0x200);
    std::memset(ram(kRec), 0, 4 + 12 * kMaxRec);
    put32(kObj + 0x64, kVtable);
    put32(kObj + 0x6C, kOrg);
    put32(kObj + 0xE400, kPauseRace);
    put32(kObj + 0xE404, kPauseSimple);
    // RaceInput__getButtonDown: (+0x1B8 ^ +0x188) & +0x188 while +0x198 bit 0. Start (0x40) and the SimplePause's bit
    // (0x80) pressed now half the time each, held from before a quarter of the time; the input off one time in eight.
    std::uint32_t now = rnd(), before = rnd();
    for (std::uint32_t bit : {0x40u, 0x80u}) {
        if (rnd() & 1) now |= bit; else now &= ~bit;
        if ((rnd() & 3) == 0) before |= bit; else before &= ~bit;
    }
    put32(kInput + 0x188, now);
    put32(kInput + 0x1B8, before);
    put32(kInput + 0x198, (rnd() & 7) ? (rnd() | 1u) : (rnd() & ~1u));
    put32(kAns, (rnd() & 3) == 0 ? 1u : 0u);       // 152: the auto demo exits (also when the run mode says play)
    put32(kAns + 4, (rnd() & 3) == 0 ? 1u : 0u);   // 23: a replay exits
    // The state's fields (the copy's gate reads them; GT4's branch reads cc8, cd0, cdc and the end delay).
    std::uint32_t mode = 0, cdc = 0, cd0 = 0, started = 1, countdown = 0;
    float end = -rndf(0.0f, 10.0f) - 0.001f;
    switch (state) {
    case 0: started = rnd() & 1; countdown = started ? rnd() % 300 : 0; break;   // past the start (flag or none left)
    case 1: started = 0; countdown = 1 + rnd() % 300; break;                      // before the start
    case 2: mode = 1; break;                                                     // B-spec
    case 3: if (rnd() & 1) mode = 2 + rnd() % 3; else cdc = 1 + (rnd() & 0xFF); break;   // replay, demo, +0xCDC
    case 4: end = rndf(0.0f, 10.0f); mode = rnd() % 5; break;                     // the end delay
    default: cd0 = 1 + (rnd() & 0xFF); mode = rnd() % 5; break;                   // visual mode: nothing runs
    }
    put32(kObj + 0xCC8, mode);
    put32(kObj + 0xCD0, cd0);
    put32(kObj + 0xCDC, cdc);
    put32(kObj + 0xD4C, started);
    put32(kOrg + 0xBC, countdown);
    putf(kOrg + 0xD4, end);
    args[0] = kObj;
}

void setup0(std::uint32_t a[8]) { setup_state(0, a); }
void setup1(std::uint32_t a[8]) { setup_state(1, a); }
void setup2(std::uint32_t a[8]) { setup_state(2, a); }
void setup3(std::uint32_t a[8]) { setup_state(3, a); }
void setup4(std::uint32_t a[8]) { setup_state(4, a); }
void setup5(std::uint32_t a[8]) { setup_state(5, a); }

Test check_pause(const char *name, void (*setup)(std::uint32_t[8])) {
    Test t{name, 0x0038B0E0, "i", 'v', false, setup};
    t.teardown = teardown;
    return t;
}

const Register r0(check_pause("0038B0E0.s0", setup0));
const Register r1(check_pause("0038B0E0.s1", setup1));
const Register r2(check_pause("0038B0E0.s2", setup2));
const Register r3(check_pause("0038B0E0.s3", setup3));
const Register r4(check_pause("0038B0E0.s4", setup4));
const Register r5(check_pause("0038B0E0.visual", setup5));

}  // namespace
