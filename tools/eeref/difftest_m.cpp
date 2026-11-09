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
    gt4::rt::log_info("difftest 0038B0E0.s%d: branches (each side): pauseStart %ld, terminate(1) %ld, "
                      "terminate(2) %ld, SimplePause %ld",
                      g_state, c[kPause] / 2, c[kTerm1] / 2, c[kTerm2] / 2, c[kSimple] / 2);
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
    case 2: mode = 1; break;                                     // B-spec: the copy's state 0 since M9d3
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

// ---- 003C3820, RacePause::update (game_src/func_003C3820.cpp, M7) ----
//
// With ui.pause = gt4 (a DIFFTEST game has no host: option 10 is 0) the copy must be GT4's function. Its calls:
// RaceEventQueue__get on arg2 + 0xF4 and PauseBase's time count and limit on the pause run for real on both sides, on
// a queue in scratch (events 0x16 and 0x17 present or not) or with arg2 = 0 and the event flags (+0x20, +0x24) preset,
// and with limits either side of the count. The strobe's three calls, 004751D0 (start), 00475488 (its rate) and
// 003EBF40 (its update), go to recorders patched at both ends: the PS2 entry (the original, through orig_bridge's
// hook) and the native function's first bytes (the copy calls it by symbol), both restored in the teardown. A call
// that bypassed a patch would leave the two logs different and fail the trial. 003EBF40's recorder answers from
// scratch, and setup sets the update object's +0x18 (the strobe ended) or not, so the copy's 'return 3' is reached.
// 00475488's float is recorded (kinds.txt has it as `if`).
// The stated gap: the copy's native branch (gt4_pause_tick >= 0: the native pause page, func_004857C8's sound) never
// runs here. With ui.pause = gt4 and no page open, gt4_pause_tick answers -1 and gt4_pause_end returns at once; the
// original has no such branch, and M7's sessions cover it. A tick that answered >= 0 would show as a differing trial
// (the copy would skip the strobe and clear +0xC where the original starts it); the teardown logs option 10.

extern "C" void func_004751D0(void *, int);
extern "C" void func_00475488(void *, float);
extern "C" int func_003EBF40(void *, void *, int);
extern "C" int gt4_game_option(int id);

constexpr std::uint32_t kRacePause = kScratch + 0x40000;     // the RacePause (fields to +0x38)
constexpr std::uint32_t kStrobe = kScratch + 0x40100;    // +0x14: the strobe (only its address is used)
constexpr std::uint32_t kUpdate = kScratch + 0x40200;    // +0x18: the update object (its +0x18: the strobe ended)
constexpr std::uint32_t kArg2 = kScratch + 0x41000;      // arg2: the race (its event queue at +0xF4)
constexpr std::uint32_t kPRec = kScratch + 0x48000;      // the recorders' log, as kRec
constexpr std::uint32_t kPAns = kScratch + 0x48400;      // 003EBF40's answer

long g_pcount[3][6] = {};   // per test: the planned exit (event, strobe ended, time limit, open), the strobe calls
int g_ptest = 0;
enum { kExitEvent, kExitStrobe, kExitLimit, kExitOpen, kCallStart, kCallUpdate };

void plog(std::uint32_t what, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2) {
    const std::uint32_t n = get32(kPRec);
    if (n < kMaxRec) {
        put32(kPRec + 4 + 16 * n, what);
        put32(kPRec + 8 + 16 * n, a0);
        put32(kPRec + 12 + 16 * n, a1);
        put32(kPRec + 16 + 16 * n, a2);
    }
    put32(kPRec, n + 1);
}
void rec_strobe_start(std::uint32_t strobe, std::int32_t a1) {
    plog(0x004751D0, strobe, static_cast<std::uint32_t>(a1), 0);
    ++g_pcount[g_ptest][kCallStart];
}
void rec_strobe_rate(std::uint32_t strobe, float rate) {
    std::uint32_t b;
    std::memcpy(&b, &rate, 4);
    plog(0x00475488, strobe, b, 0);
}
std::int32_t rec_strobe_update(std::uint32_t update, std::uint32_t strobe, std::int32_t a2) {
    plog(0x003EBF40, update, strobe, static_cast<std::uint32_t>(a2));
    ++g_pcount[g_ptest][kCallUpdate];
    return static_cast<std::int32_t>(get32(kPAns));
}

// Both ends of each strobe callee: the PS2 entry (5 bytes) and the native function's entry (5 bytes).
struct Both {
    std::uint32_t ps2;
    void *native;
    const void *recorder;
    std::uint8_t saved_ps2[5] = {}, saved_native[5] = {};
};
Both g_both[] = {
    {0x004751D0, reinterpret_cast<void *>(&func_004751D0), reinterpret_cast<const void *>(&rec_strobe_start)},
    {0x00475488, reinterpret_cast<void *>(&func_00475488), reinterpret_cast<const void *>(&rec_strobe_rate)},
    {0x003EBF40, reinterpret_cast<void *>(&func_003EBF40), reinterpret_cast<const void *>(&rec_strobe_update)},
};
bool g_both_patched = false;

void jump(std::uint8_t *at, const void *to) {
    const std::int32_t rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(to) -
                                                        (reinterpret_cast<std::uintptr_t>(at) + 5));
    at[0] = 0xE9;
    std::memcpy(at + 1, &rel, 4);
}
void write_code(void *at, const void *bytes) {
    DWORD old = 0;
    VirtualProtect(at, 5, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(at, bytes, 5);
    VirtualProtect(at, 5, old, &old);
}

void patch_both() {
    if (g_both_patched) return;
    for (Both &b : g_both) {
        std::memcpy(b.saved_ps2, ram(b.ps2), 5);
        jump(ram(b.ps2), b.recorder);
        std::memcpy(b.saved_native, b.native, 5);
        std::uint8_t j[5] = {0xE9};   // jmp rel32, relative to the native entry's own address
        const std::int32_t rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(b.recorder) -
                                                            (reinterpret_cast<std::uintptr_t>(b.native) + 5));
        std::memcpy(j + 1, &rel, 4);
        write_code(b.native, j);
    }
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    g_both_patched = true;
}

void teardown_pause() {
    if (!g_both_patched) return;
    for (Both &b : g_both) {
        std::memcpy(ram(b.ps2), b.saved_ps2, 5);
        write_code(b.native, b.saved_native);
    }
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    g_both_patched = false;
    const long *c = g_pcount[g_ptest];
    gt4::rt::log_info("difftest 003C3820.%d: planned exits (each trial): event %ld, strobe ended %ld, time limit %ld, "
                      "stays open %ld; strobe calls (each side): start %ld, update %ld; ui.pause (option 10) = %d, "
                      "so gt4_pause_tick answered -1", g_ptest, c[kExitEvent], c[kExitStrobe], c[kExitLimit],
                      c[kExitOpen], c[kCallStart] / 2, c[kCallUpdate] / 2, gt4_game_option(10));
}

// One trial. test 0: events (a queue, or arg2 = 0 with the flags preset); 1: no events, the strobe and the time limit;
// 2: both mixed.
void setup_pause(int test, std::uint32_t args[8]) {
    patch_both();
    g_ptest = test;
    std::memset(ram(kRacePause), 0, 0x40);
    std::memset(ram(kUpdate), 0, 0x40);
    std::memset(ram(kArg2), 0, 0xF4 + 0x828);
    std::memset(ram(kPRec), 0, 4 + 16 * kMaxRec);
    // PauseBase: the count at +0, the limit at +4 (0: none), on either side of the count
    const std::uint32_t count = rnd() % 100;
    const std::uint32_t limit = (rnd() & 3) == 0 ? 0 : (rnd() & 1 ? count + 1 + rnd() % 50 : rnd() % (count + 1));
    put32(kRacePause + 0x0, count);
    put32(kRacePause + 0x4, limit);
    put32(kRacePause + 0xC, rnd() & 1);                                 // the strobe started already
    put32(kRacePause + 0x14, (rnd() & 3) ? kStrobe : 0);                // the strobe
    put32(kRacePause + 0x18, (rnd() & 3) ? kUpdate : 0);                // its update object
    put32(kUpdate + 0x18, (rnd() & 3) == 0 ? 1u : 0u);              // the strobe ended
    put32(kPAns, (rnd() & 1) ? rnd() % 5 : 0);                      // 003EBF40's answer
    // the items (read only by the native branch)
    for (int f = 0x28; f <= 0x34; f += 4) put32(kRacePause + f, rnd() & 1);
    bool event = false;
    std::uint32_t arg2 = kArg2;
    const bool events = test == 0 || (test == 2 && (rnd() & 1));
    if (events && (rnd() & 1)) {
        arg2 = 0;   // no race: the flags preset
        put32(kRacePause + 0x20, rnd() & 1);
        put32(kRacePause + 0x24, rnd() & 1);
        event = get32(kRacePause + 0x20) || get32(kRacePause + 0x24);
    } else if (events) {
        // the race's event queue (arg2 + 0xF4): entries of 8 bytes from +8, refs[id] at +0x808, the count at +0x824
        const std::uint32_t q = kArg2 + 0xF4;
        std::uint32_t n = 0;
        for (std::uint16_t id : {static_cast<std::uint16_t>(0x16), static_cast<std::uint16_t>(0x17),
                                 static_cast<std::uint16_t>(0x1B)}) {
            if (rnd() & 1) {
                std::memcpy(ram(q + 8 + 8 * n), &id, 2);
                put32(q + 12 + 8 * n, rnd());
                ram(q + 0x808)[id] = 1;
                ++n;
                event = event || id != 0x1B;
            }
        }
        put32(q + 0x824, n);
    }
    // the planned exit, in the copy's order, for the tally (run() checks both sides agree on what happens): an
    // event; else the strobe ended (only with an update object); else the time limit (checked only when the update
    // answered 0); else the pause stays open
    const bool update = get32(kRacePause + 0x18) != 0;
    const bool ended = update && get32(kUpdate + 0x18);
    const bool answered = update && get32(kPAns) != 0;
    const bool hit = !answered && limit != 0 && count + 1 > limit;
    ++g_pcount[test][event ? kExitEvent : ended ? kExitStrobe : hit ? kExitLimit : kExitOpen];
    args[0] = kRacePause;
    args[1] = rnd();
    args[2] = arg2;
}
void setup_p0(std::uint32_t a[8]) { setup_pause(0, a); }
void setup_p1(std::uint32_t a[8]) { setup_pause(1, a); }
void setup_p2(std::uint32_t a[8]) { setup_pause(2, a); }

Test check_racepause(const char *name, void (*setup)(std::uint32_t[8])) {
    Test t{name, 0x003C3820, "iii", 'i', false, setup};
    t.teardown = teardown_pause;
    return t;
}
const Register p0(check_racepause("003C3820.events", setup_p0));
const Register p1(check_racepause("003C3820.strobe", setup_p1));
const Register p2(check_racepause("003C3820.mixed", setup_p2));

}  // namespace
