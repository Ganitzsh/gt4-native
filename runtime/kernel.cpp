#include "kernel.h"

#include "guest.h"
#include "image.h"
#include "link.h"
#include "gt4/link/protocol.h"
#include "log.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>

namespace gt4::rt {
uint64_t kernel_bus_cycles();
bool kernel_paused();
namespace {

// ---------------------------------------------------------------------------------------------
// State

enum ThreadStatus : int { THS_RUN = 1, THS_READY = 2, THS_WAIT = 4, THS_SUSPEND = 8, THS_WAITSUSPEND = 12, THS_DORMANT = 16 };
enum WaitType : int { WAIT_NONE = 0, WAIT_SLEEP = 1, WAIT_SEMA = 2 };

constexpr int kMaxThreads = 256;
constexpr int kMaxSemas = 256;
constexpr int kPriorities = 128;
constexpr double kBusHz = 147456000.0;
// The game's frame rate (it steps its simulation once a frame): 60 Hz, a modern display's, not NTSC's 59.94
// (docs/NATIVE_V2.md, step 4: a 60 Hz display no longer repeats a frame every 16 s, the sound mixes 800 samples a
// frame). The EE kernel's alarms count H-blanks: 262.5 a frame, as on the PS2.
constexpr double kFrameHz = 60.0;
constexpr double kHsyncHz = kFrameHz * 262.5;

// ee_thread_t / ee_thread_status_t as the game passes them (all 32-bit fields).
struct GuestThreadParam {
    int32_t status;
    uint32_t func, stack;
    int32_t stack_size;
    uint32_t gp;
    int32_t initial_priority, current_priority;
    uint32_t attr, option;
    int32_t wait_type, wait_id, wakeup_count;
};
struct GuestSemaParam {
    int32_t count, max_count, init_count, wait_threads;
    uint32_t attr, option;
};

struct Thread {
    int id = 0;
    int status = THS_DORMANT;
    int wait = WAIT_NONE, wait_id = 0;
    uint32_t entry = 0, stack = 0, gp = 0, attr = 0, option = 0;
    int32_t stack_size = 0;
    int init_prio = 0, prio = 0;
    int wakeup_count = 0;
    bool suspended = false;      // SuspendThread (guest)
    uint32_t arg = 0;
    HANDLE host = nullptr;
    HANDLE event = nullptr;      // set when the scheduler dispatches this thread
    bool host_frozen = false;    // preempted by an interrupt: its host thread is suspended
    int kernel_depth = 0;        // inside runtime code
    bool di = false;             // guest interrupts disabled
    bool used = false;
};

struct Sema {
    bool used = false;
    int count = 0, max = 0, init = 0;
    uint32_t attr = 0, option = 0;
    std::deque<int> waiters;
};

struct Alarm {
    bool used = false;
    uint64_t due_cycles = 0;
    uint32_t func = 0, arg = 0;
    uint16_t ticks = 0;
};

CRITICAL_SECTION g_lock;
Thread g_threads[kMaxThreads];
Sema g_semas[kMaxSemas];
std::deque<Thread *> g_ready[kPriorities];
Thread *g_running = nullptr;
thread_local Thread *t_self = nullptr;
std::atomic<bool> g_in_interrupt{false};
// The frame clock's callbacks (gt4_frame_tick_add), in order; a frame come due and not yet delivered.
struct FrameTick {
    int id;
    void (*fn)(uint32_t);
    uint32_t arg;
};
std::vector<FrameTick> g_ticks;
int g_next_tick_id = 1;
bool g_tick_pending = false;
Alarm g_alarms[64];
LARGE_INTEGER g_qpc_freq, g_qpc_start;
std::atomic<uint32_t> g_frames{0};
HANDLE g_irq_wake = nullptr;
struct Callback {
    void (*fn)(uint32_t);
    uint32_t arg;
};
std::vector<Callback> g_callbacks;
void (*g_interrupt_hook)() = nullptr;  // run in interrupt context (SIF completions...)
void (*g_frame_hook)(uint32_t) = nullptr;

struct Lock {
    Lock() { EnterCriticalSection(&g_lock); }
    ~Lock() { LeaveCriticalSection(&g_lock); }
};

void set_guest_fpu() {
    unsigned short cw;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    cw &= ~0x0300;  // single precision, as the EE FPU
    // rounding toward zero, as the EE FPU does for every result (x87's default, to nearest, moves the last bit:
    // a text box sized to its text no longer fits it). GT4_FPU_RC=nearest for comparison.
    cw |= 0x0C00;
    if (const char *rc = getenv("GT4_FPU_RC"); rc && rc[0] == 'n') cw &= ~0x0C00;
    if (const char *pc = getenv("GT4_FPU_PC"))  // testing: double or extended precision instead
        cw |= pc[0] == 'd' ? 0x0200 : pc[0] == 'e' ? 0x0300 : 0;
    if (const char *t = getenv("GT4_FPU_TRAP"); t && std::strncmp(t, "file:", 5) != 0)
        cw &= ~0x000D;  // testing: fault on the first invalid operation, divide by zero or overflow
                        // (the EE has no inf or NaN: it clamps; IEEE results here diverge)
    __asm__ volatile("fldcw %0" : : "m"(cw));
}

// GT4_FPU_TRAP=file:PATH (testing): the trap above only once PATH exists, for the game threads that then
// enter WaitSema; reaching a screen at full speed first, since every IEEE infinity or NaN then faults.
std::atomic<bool> g_fpu_trap_armed{false};
void arm_fpu_trap_here() {
    static thread_local bool armed = false;
    if (armed || !g_fpu_trap_armed.load(std::memory_order_relaxed)) return;
    armed = true;
    unsigned short cw;
    __asm__ volatile("fnclex\n\tfnstcw %0" : "=m"(cw));
    cw &= ~0x000D;
    __asm__ volatile("fldcw %0" : : "m"(cw));
}

Thread *thread_by_id(int id) {
    if (id <= 0 || id >= kMaxThreads || !g_threads[id].used) return nullptr;
    return &g_threads[id];
}

Thread *self() { return t_self; }

// ---------------------------------------------------------------------------------------------
// Scheduler (callers hold g_lock)

Thread *best_ready() {
    for (auto &q : g_ready)
        if (!q.empty()) return q.front();
    return nullptr;
}

void remove_ready(Thread *t) {
    auto &q = g_ready[t->prio];
    for (auto it = q.begin(); it != q.end(); ++it)
        if (*it == t) {
            q.erase(it);
            return;
        }
}

void make_ready(Thread *t, bool front = false) {
    t->status = THS_READY;
    if (front)
        g_ready[t->prio].push_front(t);
    else
        g_ready[t->prio].push_back(t);
}

void dispatch(Thread *t) {
    remove_ready(t);
    t->status = THS_RUN;
    g_running = t;
    if (t->host_frozen) {
        t->host_frozen = false;
        ResumeThread(t->host);
    } else {
        SetEvent(t->event);
    }
}

// The calling guest thread gives up the CPU (its status is already set: READY or WAIT or DORMANT)
// and waits until it is dispatched again. Called with g_lock held exactly once.
void switch_away(Thread *me) {
    if (g_running == me) g_running = nullptr;
    if (Thread *n = best_ready()) dispatch(n);
    LeaveCriticalSection(&g_lock);
    WaitForSingleObject(me->event, INFINITE);
    EnterCriticalSection(&g_lock);
}

// After making `t` ready from a running thread: preempt the caller if `t` has a higher priority.
void maybe_preempt() {
    if (g_in_interrupt) return;  // decided when the interrupt returns
    Thread *me = self();
    Thread *best = best_ready();
    if (!me || !best || me != g_running || best->prio >= me->prio) return;
    make_ready(me, true);
    switch_away(me);
}

void wake(Thread *t) {
    if (t->suspended) {
        t->status = THS_SUSPEND;
        t->wait = WAIT_NONE;
        return;
    }
    t->wait = WAIT_NONE;
    make_ready(t);
}

// ---------------------------------------------------------------------------------------------
// Interrupts

using AlarmFn = void (*)(int id, uint16_t ticks, uint32_t arg);

// Runs what came due (the caller holds g_lock and has made sure no guest thread runs): the frame's callbacks,
// the runtime's completions, the alarms. Nothing here may allocate or free memory: the guest thread was frozen
// wherever it was, possibly inside the C runtime's heap holding its lock.
void deliver_pending() {
    g_in_interrupt = true;
    if (g_tick_pending) {
        g_tick_pending = false;
        for (size_t k = 0; k < g_ticks.size(); ++k) g_ticks[k].fn(g_ticks[k].arg);
    }
    if (g_interrupt_hook) g_interrupt_hook();
    const size_t posted = g_callbacks.size();   // what was posted until now (a callback may post more)
    for (size_t k = 0; k < posted; ++k) {
        const Callback c = g_callbacks[k];
        c.fn(c.arg);
    }
    g_callbacks.erase(g_callbacks.begin(), g_callbacks.begin() + static_cast<std::ptrdiff_t>(posted));   // keeps the storage
    const uint64_t now = kernel_bus_cycles();
    for (int i = 0; i < 64; ++i) {
        Alarm &a = g_alarms[i];
        if (a.used && a.due_cycles <= now) {
            a.used = false;
            reinterpret_cast<AlarmFn>(guest_ptr(a.func))(i, a.ticks, a.arg);
        }
    }
    g_in_interrupt = false;
}

// How the frame's callbacks, completions and alarms reach a running guest thread (docs/NATIVE_V2.md, step 4):
// cooperative (the default, GT4_FRAME_DELIVERY=cooperative): at its next safe point, a syscall or the end of runtime
// code (deliver_on_self), as calls in its own flow, no thread frozen at an arbitrary instruction; kept waiting longer
// than kCooperativeLimit (a thread that never makes a syscall), they are delivered the PS2's way after all.
// preemptive: the PS2's interrupts, always.
bool cooperative_delivery() {
    static const bool on = [] { const char *e = getenv("GT4_FRAME_DELIVERY"); return !(e && !strcmp(e, "preemptive")); }();
    return on;
}
constexpr uint64_t kCooperativeLimit = static_cast<uint64_t>(kBusHz * 0.05);   // 50 ms of bus cycles
uint64_t g_due_since = 0;   // when something to deliver came due (0: nothing waits)
uint64_t g_delivered_cooperatively = 0, g_delivered_preemptively = 0;

// From the interrupt thread: freeze the running guest thread, run handlers, resume or preempt. False when it left
// them for the running thread's next safe point.
bool interrupt_now() {
    Lock lock;
    Thread *r = g_running;
    if (r && (r->kernel_depth > 0 || r->di)) return false;  // delivered when it leaves (KernelScope/EI)
    if (!g_due_since) g_due_since = kernel_bus_cycles();
    if (r && cooperative_delivery() && kernel_bus_cycles() - g_due_since < kCooperativeLimit) return false;
    g_due_since = 0;
    if (r) ++g_delivered_preemptively;
    if (r) {
        SuspendThread(r->host);
        CONTEXT ctx;
        ctx.ContextFlags = CONTEXT_CONTROL;
        GetThreadContext(r->host, &ctx);  // makes the suspension take effect
    }
    deliver_pending();
    Thread *best = best_ready();
    if (r) {
        if (best && best->prio < r->prio) {
            r->host_frozen = true;
            make_ready(r, true);
            g_running = nullptr;
            dispatch(best);
        } else {
            ResumeThread(r->host);
        }
    } else if (best && !g_running) {
        dispatch(best);
    }
    return true;
}

bool g_alarms_due() {
    if (!g_callbacks.empty() || g_tick_pending) return true;
    const uint64_t now = kernel_bus_cycles();
    for (const Alarm &a : g_alarms)
        if (a.used && a.due_cycles <= now) return true;
    return false;
}

// Delivers held interrupts on the calling guest thread (leaving runtime code, or EI).
void deliver_on_self(Thread *me) {
    EnterCriticalSection(&g_lock);
    if (g_alarms_due()) {
        // the handlers run on this thread: a syscall or EI they make must not deliver again (it
        // would take g_lock a second time, and switch_away below releases it only once)
        ++me->kernel_depth;
        deliver_pending();
        --me->kernel_depth;
        g_due_since = 0;
        ++g_delivered_cooperatively;
        Thread *best = best_ready();
        if (best && me == g_running && best->prio < me->prio) {
            make_ready(me, true);
            switch_away(me);
        }
    }
    LeaveCriticalSection(&g_lock);
}

DWORD WINAPI interrupt_thread(void *) {
    set_guest_fpu();
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    const uint64_t frame_cycles = static_cast<uint64_t>(kBusHz / kFrameHz);
    uint64_t next_frame = frame_cycles;
    for (;;) {
        if (kernel_paused()) {   // nothing comes due; keep reading the host's records (the resume)
            Sleep(5);
            link_poll();
            continue;
        }
        uint64_t now = kernel_bus_cycles();
        uint64_t due = next_frame;
        {
            Lock lock;
            for (const Alarm &a : g_alarms)
                if (a.used) due = std::min(due, a.due_cycles);
        }
        if (due > now) {
            LARGE_INTEGER rel;
            rel.QuadPart = -static_cast<LONGLONG>((due - now) * 1e7 / kBusHz);
            if (rel.QuadPart == 0) rel.QuadPart = -1;
            SetWaitableTimer(timer, &rel, 0, nullptr, nullptr, FALSE);
            HANDLE hs[2] = {timer, g_irq_wake};
            WaitForMultipleObjects(2, hs, FALSE, INFINITE);
            now = kernel_bus_cycles();
        }
        {
            Lock lock;
            if (now >= next_frame) {   // a frame: the runtime's frame end (display, host), then the game's callbacks
                g_tick_pending = true;
                ++g_frames;
                if (g_frame_hook) g_frame_hook(g_frames);
                static const char *trap = getenv("GT4_FPU_TRAP");
                if (trap && !std::strncmp(trap, "file:", 5) && g_frames % 30 == 0 && !g_fpu_trap_armed &&
                    GetFileAttributesA(trap + 5) != INVALID_FILE_ATTRIBUTES) {
                    g_fpu_trap_armed = true;
                    log_info("x87 trap armed at frame %u", g_frames.load());
                }
                next_frame += frame_cycles;
                if (next_frame < now) next_frame = now + frame_cycles;  // fell behind: skip
            }
        }
        // (left for a safe point: look again shortly, for the limit)
        if (g_alarms_due() && !interrupt_now()) WaitForSingleObject(g_irq_wake, 1);
        if (g_frames % 3600 == 0 && now >= next_frame - frame_cycles && g_frames) {
            static uint32_t logged = 0;
            if (logged != g_frames) {
                logged = g_frames;
                log_info("kernel: frame callbacks delivered at safe points %llu, by interrupting %llu",
                         static_cast<unsigned long long>(g_delivered_cooperatively), static_cast<unsigned long long>(g_delivered_preemptively));
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Guest thread bodies

DWORD WINAPI guest_thread_main(void *p) {
    Thread *t = static_cast<Thread *>(p);
    t_self = t;
    set_guest_fpu();
    WaitForSingleObject(t->event, INFINITE);  // first dispatch
    auto entry = reinterpret_cast<void (*)(uint32_t)>(guest_ptr(t->entry));
    entry(t->arg);
    // returning from the entry function is ExitThread
    EnterCriticalSection(&g_lock);
    t->status = THS_DORMANT;
    if (g_running == t) g_running = nullptr;
    if (Thread *n = best_ready()) dispatch(n);
    LeaveCriticalSection(&g_lock);
    return 0;
}

int alloc_thread_id() {
    for (int i = 2; i < kMaxThreads; ++i)
        if (!g_threads[i].used) return i;
    return -1;
}

[[noreturn]] void exit_current_thread() {
    Thread *me = self();
    EnterCriticalSection(&g_lock);
    me->status = THS_DORMANT;
    if (g_running == me) g_running = nullptr;
    if (Thread *n = best_ready()) dispatch(n);
    LeaveCriticalSection(&g_lock);
    ExitThread(0);
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Public kernel interface

KernelScope::KernelScope() {
    if (Thread *t = self()) ++t->kernel_depth;
}
KernelScope::~KernelScope() {
    Thread *t = self();
    if (!t) return;
    if (--t->kernel_depth == 0 && !t->di && t == g_running && !g_in_interrupt) {
        if (g_alarms_due()) deliver_on_self(t);
    }
}

int kernel_di() {
    Thread *t = self();
    if (!t) return 0;
    const bool was = !t->di;
    t->di = true;
    return was;
}

int kernel_ei() {
    Thread *t = self();
    if (!t) return 1;
    const bool was = !t->di;
    t->di = false;
    if (t->kernel_depth == 0 && !g_in_interrupt && g_alarms_due())
        deliver_on_self(t);
    return was;
}

bool kernel_in_interrupt() { return g_in_interrupt; }

bool kernel_interrupts_enabled() {
    Thread *t = self();
    return t && !t->di && !g_in_interrupt;
}

void kernel_set_frame_hook(void (*fn)(uint32_t)) { g_frame_hook = fn; }
void kernel_set_interrupt_hook(void (*fn)()) { g_interrupt_hook = fn; }

void kernel_post_callback(void (*fn)(uint32_t), uint32_t arg) {
    {
        Lock lock;
        g_callbacks.push_back({fn, arg});
    }
    SetEvent(g_irq_wake);
}

// The host's settings menu pauses the game (GT4_REC_HOST_CMD 2 / 3): the bus clock stands still, so no
// frame, alarm or timer comes due, and the time spent paused is left out afterwards.
namespace {
std::atomic<bool> g_paused{false};
std::atomic<int64_t> g_paused_ticks{0};   // QPC ticks spent paused before the current pause
int64_t g_pause_started = 0;
std::atomic<bool> g_stalled{false};       // kernel_add_stall was used: the clock is kept monotonic
std::atomic<uint64_t> g_last_cycles{0};
}  // namespace

uint64_t kernel_bus_cycles() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    int64_t ticks = now.QuadPart;
    if (g_paused.load(std::memory_order_acquire)) ticks = g_pause_started;
    ticks -= g_qpc_start.QuadPart + g_paused_ticks.load(std::memory_order_acquire);
    uint64_t cycles = static_cast<uint64_t>(ticks * (kBusHz / g_qpc_freq.QuadPart));
    if (g_stalled.load(std::memory_order_relaxed)) {  // a stall taken off may not turn the clock back
        uint64_t prev = g_last_cycles.load();
        while (cycles > prev && !g_last_cycles.compare_exchange_weak(prev, cycles)) {}
        cycles = std::max(cycles, prev);
    }
    return cycles;
}

// Time the game did not run (a debugging tool's faults), taken off the bus clock like a pause,
// so that frame-driven game logic stays closer to its usual pace while the tool slows the game down.
void kernel_add_stall(int64_t qpc_ticks) {
    if (qpc_ticks <= 0) return;
    g_stalled.store(true, std::memory_order_relaxed);
    g_paused_ticks.fetch_add(qpc_ticks, std::memory_order_acq_rel);
}

void kernel_set_paused(bool paused) {
    if (paused == g_paused.load()) return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (paused) {
        g_pause_started = now.QuadPart;
        g_paused.store(true, std::memory_order_release);
        log_info("kernel: paused by the host");
    } else {
        // The offset first: until the flag clears, readers still see the frozen time.
        g_paused_ticks.fetch_add(now.QuadPart - g_pause_started, std::memory_order_acq_rel);
        g_paused.store(false, std::memory_order_release);
        SetEvent(g_irq_wake);
        log_info("kernel: resumed by the host");
    }
}

bool kernel_paused() { return g_paused.load(std::memory_order_acquire); }

uint32_t kernel_frame_count() { return g_frames; }

void kernel_init() {
    InitializeCriticalSection(&g_lock);
    QueryPerformanceFrequency(&g_qpc_freq);
    QueryPerformanceCounter(&g_qpc_start);
    g_irq_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    // The main thread (id 1), already running on the calling host thread.
    Thread &m = g_threads[1];
    m.used = true;
    m.id = 1;
    m.init_prio = m.prio = 1;
    m.status = THS_RUN;
    m.entry = 0x00100008;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &m.host, 0, FALSE,
                    DUPLICATE_SAME_ACCESS);
    m.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    t_self = &m;
    g_running = &m;
}

namespace {
// GT4_WATCH=seconds: logs every guest thread's state and where the running one is (sampled).
DWORD WINAPI watchdog_thread(void *p) {
    const DWORD period = static_cast<DWORD>(reinterpret_cast<uintptr_t>(p)) * 1000;
    for (;;) {
        Sleep(period);
        char line[1024];
        int n = snprintf(line, sizeof line, "watch: frame %u, threads:", g_frames.load());
        uint32_t pc = 0;
        int running = 0;
        char chain[600] = "";
        int chain_len = 0;
        // a kernel lock held for 2 s is a deadlock: name its owner and where it is (stderr only,
        // since the log itself may be what is blocked)
        bool locked = false;
        for (int tries = 0; tries < 200 && !(locked = TryEnterCriticalSection(&g_lock)); ++tries) Sleep(10);
        if (!locked) {
            const DWORD owner = static_cast<DWORD>(reinterpret_cast<uintptr_t>(g_lock.OwningThread));
            fprintf(stderr, "watch: kernel lock held for 2 s by host thread %lu", owner);
            if (HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, owner)) {
                if (SuspendThread(h) != static_cast<DWORD>(-1)) {
                    CONTEXT ctx;
                    ctx.ContextFlags = CONTEXT_CONTROL;
                    if (GetThreadContext(h, &ctx)) {
                        uint32_t g = 0;
                        const char *nm = native_name_for(ctx.Eip, &g);
                        fprintf(stderr, " at %08lx (%s, PS2 %08x); stack:", ctx.Eip, nm, g);
                        const auto *sp = reinterpret_cast<const uint32_t *>(ctx.Esp);
                        MEMORY_BASIC_INFORMATION mi;
                        for (int k = 0, shown = 0; k < 4096 && shown < 16; ++k) {
                            if ((k == 0 || (reinterpret_cast<uintptr_t>(sp + k) & 0xFFF) == 0) &&
                                (!VirtualQuery(sp + k, &mi, sizeof mi) || mi.State != MEM_COMMIT))
                                break;
                            if (sp[k] < 0x02000000 || sp[k] >= 0x04000000) continue;
                            const char *n2 = native_name_for(sp[k], &g);
                            if (*n2) {
                                fprintf(stderr, " %08x(%s)", sp[k], g ? n2 + (std::strncmp(n2, "gt4fn_", 6) ? 0 : 6) : n2);
                                ++shown;
                            }
                        }
                    }
                    ResumeThread(h);
                }
                CloseHandle(h);
            }
            fprintf(stderr, "\n");
            fflush(stderr);
            continue;
        }
        LeaveCriticalSection(&g_lock);
        {
            Lock lock;
            for (int i = 1; i < kMaxThreads && n < 900; ++i) {
                const Thread &t = g_threads[i];
                if (!t.used) continue;
                n += snprintf(line + n, sizeof line - n, " %d:%s/p%d%s", t.id,
                              t.status == THS_RUN ? "run" : t.status == THS_READY ? "ready"
                              : t.status == THS_WAIT ? (t.wait == WAIT_SEMA ? "sema" : "sleep")
                              : t.status == THS_DORMANT ? "dormant" : "susp",
                              t.prio, t.kernel_depth ? "(k)" : "");
                if (t.status == THS_WAIT && t.wait == WAIT_SEMA)   // which semaphore, and its count
                    n += snprintf(line + n, sizeof line - n, "[s%d c%d]", t.wait_id, g_semas[t.wait_id].count);
            }
            if (Thread *r = g_running) {
                running = r->id;
                if (!r->host_frozen && SuspendThread(r->host) != static_cast<DWORD>(-1)) {
                    CONTEXT ctx;
                    ctx.ContextFlags = CONTEXT_CONTROL;
                    if (GetThreadContext(r->host, &ctx)) {
                        pc = ctx.Eip;
                        // its call chain through frame pointers ([ebp] = caller's ebp, [ebp+4] = return)
                        uint32_t bp = ctx.Ebp;
                        MEMORY_BASIC_INFORMATION mi;
                        for (int d = 0; d < 24 && chain_len < 560; ++d) {
                            const auto *f = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(bp));
                            if (bp < 0x10000 || (bp & 3) || !VirtualQuery(f, &mi, sizeof mi) || mi.State != MEM_COMMIT ||
                                reinterpret_cast<uintptr_t>(f + 2) > reinterpret_cast<uintptr_t>(mi.BaseAddress) + mi.RegionSize)
                                break;
                            uint32_t g = 0;
                            native_name_for(f[1], &g);
                            chain_len += snprintf(chain + chain_len, sizeof chain - chain_len, " %08x", g);
                            if (f[0] <= bp) break;
                            bp = f[0];
                        }
                    }
                    ResumeThread(r->host);
                }
            }
        }
        uint32_t guest = 0;
        const char *name = native_name_for(pc, &guest);
        log_info("%s; running %d at %08x (%s, PS2 %08x); callers%s", line, running, pc, *name ? name : "?", guest, chain);
        // where each waiting thread is: the game functions on its stack (newest first)
        for (int i = 1; i < kMaxThreads; ++i) {
            Thread &t = g_threads[i];
            if (!t.used || !t.host || t.status == THS_RUN || t.status == THS_DORMANT) continue;
            if (SuspendThread(t.host) == static_cast<DWORD>(-1)) continue;
            CONTEXT ctx;
            ctx.ContextFlags = CONTEXT_CONTROL;
            char frames[600];
            int m = 0;
            if (GetThreadContext(t.host, &ctx)) {
                const auto *sp = reinterpret_cast<const uint32_t *>(ctx.Esp);
                MEMORY_BASIC_INFORMATION mi;
                int shown = 0;
                for (int k = 0; k < 8192 && shown < 10 && m < 560; ++k) {
                    if ((k == 0 || (reinterpret_cast<uintptr_t>(sp + k) & 0xFFF) == 0) &&
                        (!VirtualQuery(sp + k, &mi, sizeof mi) || mi.State != MEM_COMMIT ||
                         (mi.Protect & (PAGE_GUARD | PAGE_NOACCESS))))
                        break;
                    uint32_t g = 0;
                    const char *nm = native_name_for(sp[k], &g);
                    if (*nm && g && std::strcmp(nm, "runtime") != 0) {
                        m += snprintf(frames + m, sizeof frames - m, " %08x", g);
                        ++shown;
                    }
                }
            }
            ResumeThread(t.host);
            frames[m] = 0;
            log_info("watch:   thread %d:%s", t.id, frames);
        }
    }
}
}  // namespace

void kernel_start_interrupts() {
    HANDLE h = CreateThread(nullptr, 1u << 20, interrupt_thread, nullptr, 0, nullptr);
    if (!h) fatal("cannot start the interrupt thread");
    log_info("kernel: frame clock at %.2f Hz", kFrameHz);
    if (const char *w = getenv("GT4_WATCH"))
        CreateThread(nullptr, 1u << 16, watchdog_thread, reinterpret_cast<void *>(static_cast<uintptr_t>(atoi(w))), 0,
                     nullptr);
}

// ---------------------------------------------------------------------------------------------
// Syscalls. Signatures as the game calls them (cdecl, 32-bit arguments). Each takes g_lock and is a
// KernelScope (no interrupt delivery inside).

#define SYSCALL extern "C" __attribute__((used))
#define KERNEL_ENTRY KernelScope ks_; Lock lk_

SYSCALL int gt4_sys_CreateThread(GuestThreadParam *p) {
    KERNEL_ENTRY;
    int id = alloc_thread_id();
    if (id < 0) return -1;
    Thread &t = g_threads[id];
    t = Thread{};
    t.used = true;
    t.id = id;
    t.entry = p->func;
    t.stack = p->stack;
    t.stack_size = p->stack_size;
    t.gp = p->gp;
    t.init_prio = t.prio = p->initial_priority & (kPriorities - 1);
    t.attr = p->attr;
    t.option = p->option;
    t.status = THS_DORMANT;
    t.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return id;
}

SYSCALL int gt4_sys_DeleteThread(int id) {
    KERNEL_ENTRY;
    Thread *t = thread_by_id(id);
    if (!t || t->status != THS_DORMANT) return -1;
    if (t->host) CloseHandle(t->host);
    CloseHandle(t->event);
    t->used = false;
    return id;
}

SYSCALL int gt4_sys_StartThread(int id, uint32_t arg) {
    KernelScope ks;
    EnterCriticalSection(&g_lock);
    Thread *t = thread_by_id(id);
    if (!t || t->status != THS_DORMANT) {
        LeaveCriticalSection(&g_lock);
        return -1;
    }
    t->arg = arg;
    t->prio = t->init_prio;
    t->wakeup_count = 0;
    if (t->host) CloseHandle(t->host);
    t->host = CreateThread(nullptr, 16u << 20, guest_thread_main, t, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    make_ready(t);
    maybe_preempt();
    LeaveCriticalSection(&g_lock);
    return id;
}

SYSCALL void gt4_sys_ExitThread() {
    exit_current_thread();
}

SYSCALL int gt4_sys_ExitDeleteThread() {
    Thread *me = self();
    EnterCriticalSection(&g_lock);
    me->used = false;
    LeaveCriticalSection(&g_lock);
    exit_current_thread();
}

SYSCALL int gt4_sys_TerminateThread(int id) {
    KERNEL_ENTRY;
    Thread *t = thread_by_id(id);
    if (!t || t == self()) return -1;
    if (t->status == THS_READY) remove_ready(t);
    if (t->wait == WAIT_SEMA) {
        auto &w = g_semas[t->wait_id].waiters;
        for (auto it = w.begin(); it != w.end(); ++it)
            if (*it == id) {
                w.erase(it);
                break;
            }
    }
    if (t->host) {
        TerminateThread(t->host, 0);
        CloseHandle(t->host);
        t->host = nullptr;
    }
    t->host_frozen = false;
    t->status = THS_DORMANT;
    t->wait = WAIT_NONE;
    ResetEvent(t->event);
    return id;
}

SYSCALL int gt4_sys_GetThreadId() {
    Thread *t = self();
    return t ? t->id : 0;
}

SYSCALL int gt4_sys_ChangeThreadPriority(int id, int prio) {
    KernelScope ks;
    EnterCriticalSection(&g_lock);
    Thread *t = id == 0 ? self() : thread_by_id(id);
    if (!t) {
        LeaveCriticalSection(&g_lock);
        return -1;
    }
    const int old = t->prio;
    prio &= kPriorities - 1;
    if (t->status == THS_READY) {
        remove_ready(t);
        t->prio = prio;
        make_ready(t);
    } else {
        t->prio = prio;
    }
    if (t == self() && t == g_running) {
        Thread *best = best_ready();
        if (best && best->prio < t->prio) {
            make_ready(t);
            switch_away(t);
        }
    } else {
        maybe_preempt();
    }
    LeaveCriticalSection(&g_lock);
    return old;
}

SYSCALL int gt4_sys_RotateThreadReadyQueue(int prio) {
    KernelScope ks;
    EnterCriticalSection(&g_lock);
    Thread *me = self();
    if (me && me == g_running && me->prio == prio && !g_ready[prio].empty()) {
        make_ready(me);  // back of its queue
        switch_away(me);
    } else if (!g_ready[prio & 127].empty()) {
        auto &q = g_ready[prio & 127];
        q.push_back(q.front());
        q.pop_front();
    }
    LeaveCriticalSection(&g_lock);
    return prio;
}

SYSCALL int gt4_sys_ReleaseWaitThread(int id) {
    KernelScope ks;
    EnterCriticalSection(&g_lock);
    Thread *t = thread_by_id(id);
    int r = -1;
    if (t && (t->status == THS_WAIT || t->status == THS_WAITSUSPEND)) {
        if (t->wait == WAIT_SEMA) {
            auto &w = g_semas[t->wait_id].waiters;
            for (auto it = w.begin(); it != w.end(); ++it)
                if (*it == id) {
                    w.erase(it);
                    break;
                }
        }
        wake(t);
        maybe_preempt();
        r = id;
    }
    LeaveCriticalSection(&g_lock);
    return r;
}

SYSCALL int gt4_sys_ReferThreadStatus(int id, GuestThreadParam *out) {
    KERNEL_ENTRY;
    Thread *t = id == 0 ? self() : thread_by_id(id);
    if (!t) return -1;
    if (out) {
        out->status = t->status;
        out->func = t->entry;
        out->stack = t->stack;
        out->stack_size = t->stack_size;
        out->gp = t->gp;
        out->initial_priority = t->init_prio;
        out->current_priority = t->prio;
        out->attr = t->attr;
        out->option = t->option;
        out->wait_type = t->wait;
        out->wait_id = t->wait_id;
        out->wakeup_count = t->wakeup_count;
    }
    return t->status;
}

SYSCALL int gt4_sys_SleepThread() {
    KernelScope ks;
    EnterCriticalSection(&g_lock);
    Thread *me = self();
    if (me->wakeup_count > 0) {
        --me->wakeup_count;
    } else {
        me->status = THS_WAIT;
        me->wait = WAIT_SLEEP;
        switch_away(me);
    }
    LeaveCriticalSection(&g_lock);
    return me->id;
}

SYSCALL int gt4_sys_WakeupThread(int id) {
    KernelScope ks;
    EnterCriticalSection(&g_lock);
    Thread *t = thread_by_id(id);
    int r = -1;
    if (t && t != self()) {
        if ((t->status == THS_WAIT || t->status == THS_WAITSUSPEND) && t->wait == WAIT_SLEEP)
            wake(t);
        else
            ++t->wakeup_count;
        maybe_preempt();
        r = id;
    }
    LeaveCriticalSection(&g_lock);
    return r;
}

SYSCALL int gt4_sys_CancelWakeupThread(int id) {
    KERNEL_ENTRY;
    Thread *t = id == 0 ? self() : thread_by_id(id);
    if (!t) return -1;
    const int n = t->wakeup_count;
    t->wakeup_count = 0;
    return n;
}

SYSCALL int gt4_sys_SuspendThread(int id) {
    KERNEL_ENTRY;
    Thread *t = thread_by_id(id);
    if (!t || t == self()) return -1;
    t->suspended = true;
    if (t->status == THS_READY) {
        remove_ready(t);
        t->status = THS_SUSPEND;
    } else if (t->status == THS_WAIT) {
        t->status = THS_WAITSUSPEND;
    }
    return id;
}

SYSCALL int gt4_sys_ResumeThread(int id) {
    KernelScope ks;
    EnterCriticalSection(&g_lock);
    Thread *t = thread_by_id(id);
    int r = -1;
    if (t && t->suspended) {
        t->suspended = false;
        if (t->status == THS_SUSPEND)
            make_ready(t);
        else if (t->status == THS_WAITSUSPEND)
            t->status = THS_WAIT;
        maybe_preempt();
        r = id;
    }
    LeaveCriticalSection(&g_lock);
    return r;
}

SYSCALL int gt4_sys_CreateSema(GuestSemaParam *p) {
    KERNEL_ENTRY;
    for (int i = 1; i < kMaxSemas; ++i) {
        Sema &s = g_semas[i];
        if (s.used) continue;
        s = Sema{};
        s.used = true;
        s.count = s.init = p->init_count;
        s.max = p->max_count;
        s.attr = p->attr;
        s.option = p->option;
        return i;
    }
    return -1;
}

SYSCALL int gt4_sys_DeleteSema(int id) {
    KernelScope ks;
    EnterCriticalSection(&g_lock);
    int r = -1;
    if (id > 0 && id < kMaxSemas && g_semas[id].used) {
        Sema &s = g_semas[id];
        for (int w : s.waiters)
            if (Thread *t = thread_by_id(w)) wake(t);
        s.waiters.clear();
        s.used = false;
        maybe_preempt();
        r = id;
    }
    LeaveCriticalSection(&g_lock);
    return r;
}

SYSCALL int gt4_sys_SignalSema(int id) {
    KernelScope ks;
    EnterCriticalSection(&g_lock);
    int r = -1;
    if (id > 0 && id < kMaxSemas && g_semas[id].used) {
        Sema &s = g_semas[id];
        if (!s.waiters.empty()) {
            Thread *t = thread_by_id(s.waiters.front());
            s.waiters.pop_front();
            if (t) wake(t);
            maybe_preempt();
        } else if (s.count < s.max || s.max <= 0) {
            ++s.count;
        }
        r = id;
    }
    LeaveCriticalSection(&g_lock);
    return r;
}

SYSCALL int gt4_sys_WaitSema(int id) {
    arm_fpu_trap_here();
    KernelScope ks;
    EnterCriticalSection(&g_lock);
    int r = -1;
    if (id > 0 && id < kMaxSemas && g_semas[id].used) {
        Sema &s = g_semas[id];
        if (s.count > 0) {
            --s.count;
        } else {
            Thread *me = self();
            me->status = THS_WAIT;
            me->wait = WAIT_SEMA;
            me->wait_id = id;
            s.waiters.push_back(me->id);
            switch_away(me);
        }
        r = id;
    }
    LeaveCriticalSection(&g_lock);
    return r;
}

SYSCALL int gt4_sys_PollSema(int id) {
    KERNEL_ENTRY;
    if (id <= 0 || id >= kMaxSemas || !g_semas[id].used) return -1;
    Sema &s = g_semas[id];
    if (s.count <= 0) return -1;
    --s.count;
    return id;
}

SYSCALL int gt4_sys_ReferSemaStatus(int id, GuestSemaParam *out) {
    KERNEL_ENTRY;
    if (id <= 0 || id >= kMaxSemas || !g_semas[id].used) return -1;
    Sema &s = g_semas[id];
    if (out) {
        out->count = s.count;
        out->max_count = s.max;
        out->init_count = s.init;
        out->wait_threads = static_cast<int32_t>(s.waiters.size());
        out->attr = s.attr;
        out->option = s.option;
    }
    return id;
}

// The PS2's interrupt controller and DMA controller have no counterpart natively: game code takes its once-a-frame
// work from the frame clock (gt4_frame_tick_add) and nothing raises the hardware's causes. Adding a handler stops
// the game, naming its caller (the handler would never run); removing, enabling or disabling one does nothing.
extern "C" void gt4_not_native(uint32_t fn, const void *caller, const char *why);
SYSCALL int gt4_sys_AddIntcHandler(int cause, uint32_t func, int, uint32_t) {
    char why[112];
    std::snprintf(why, sizeof why, "AddIntcHandler(%d, %08x): no interrupt controller natively (gt4_frame_tick_add)", cause, func);
    gt4_not_native(func, __builtin_return_address(0), why);
    return -1;
}
SYSCALL int gt4_sys_AddDmacHandler(int channel, uint32_t func, int, uint32_t) {
    char why[112];
    std::snprintf(why, sizeof why, "AddDmacHandler(%d, %08x): no DMA controller natively", channel, func);
    gt4_not_native(func, __builtin_return_address(0), why);
    return -1;
}
SYSCALL int gt4_sys_RemoveIntcHandler(int, int) { return 0; }
SYSCALL int gt4_sys_RemoveDmacHandler(int, int) { return 0; }
SYSCALL int gt4_sys_EnableIntc(int) { return 0; }
SYSCALL int gt4_sys_DisableIntc(int) { return 0; }
SYSCALL int gt4_sys_EnableDmac(int) { return 0; }
SYSCALL int gt4_sys_DisableDmac(int) { return 0; }
SYSCALL int gt4_sys_EnableIntcHandler(int, int) { return 0; }
SYSCALL int gt4_sys_DisableIntcHandler(int, int) { return 0; }
SYSCALL int gt4_sys_EnableDmacHandler(int, int) { return 0; }
SYSCALL int gt4_sys_DisableDmacHandler(int, int) { return 0; }

// The frame clock's callbacks (compat/game/gt4_prelude.h).
extern "C" int gt4_frame_tick_add(void (*fn)(uint32_t), uint32_t arg, int first) {
    Lock lock;
    const FrameTick t{g_next_tick_id++, fn, arg};
    if (first) g_ticks.insert(g_ticks.begin(), t);
    else g_ticks.push_back(t);
    return t.id;
}
extern "C" void gt4_frame_tick_remove(int id) {
    Lock lock;
    for (auto it = g_ticks.begin(); it != g_ticks.end(); ++it)
        if (it->id == id) {
            g_ticks.erase(it);
            return;
        }
}

SYSCALL int gt4_sys_SetAlarm(uint32_t ticks, uint32_t func, uint32_t arg) {
    KERNEL_ENTRY;
    for (int i = 0; i < 64; ++i) {
        Alarm &a = g_alarms[i];
        if (a.used) continue;
        a.used = true;
        a.ticks = static_cast<uint16_t>(ticks);
        a.func = func;
        a.arg = arg;
        a.due_cycles = kernel_bus_cycles() + static_cast<uint64_t>((ticks & 0xFFFF) * (kBusHz / kHsyncHz));
        SetEvent(g_irq_wake);
        return i;
    }
    return -1;
}

SYSCALL int gt4_sys_ReleaseAlarm(int id) {
    KERNEL_ENTRY;
    if (id < 0 || id >= 64 || !g_alarms[id].used) return -1;
    g_alarms[id].used = false;
    return id;
}

SYSCALL int gt4_sys_FlushCache(int) { return 0; }
SYSCALL int gt4_sys_SetGsCrt(int interlace, int mode, int field) {
    log_info("kernel: SetGsCrt(interlace %d, mode %d, field %d)", interlace, mode, field);
    // the video mode is the display's (gs.cpp, the library's SMODE2): nothing to set
    return 0;
}
// The GS interrupt mask the game sets (the kernel's reset value: every GS interrupt masked). Natively nothing raises
// a GS interrupt; the mask is kept for the game to read back.
static uint32_t g_gs_imr = 0xFF00;
SYSCALL uint32_t gt4_sys_GsGetIMR() { return g_gs_imr; }
SYSCALL uint32_t gt4_sys_GsPutIMR(uint32_t imr) {
    const uint32_t old = g_gs_imr;
    g_gs_imr = imr;
    return old;
}
SYSCALL int gt4_sys_SetVSyncFlag(uint32_t, uint32_t) { return 0; }
SYSCALL int gt4_sys_GetMemorySize() { return 32 << 20; }
SYSCALL int gt4_sys_MachineType() { return 0; }
SYSCALL int gt4_sys_EnableDispatchThread() { return 0; }
SYSCALL int gt4_sys_DisableDispatchThread() { return 0; }
SYSCALL int gt4_sys_EnableCache(int) { return 0; }
SYSCALL int gt4_sys_DisableCache(int) { return 0; }
SYSCALL uint32_t gt4_sys_SetupThread(uint32_t, uint32_t stack, int32_t size, uint32_t, uint32_t) {
    return stack + size;  // the stack pointer crt0 would use
}
SYSCALL uint32_t gt4_sys_SetupHeap(uint32_t heap, int32_t) { return heap; }
SYSCALL uint32_t gt4_sys_EndOfHeap() { return 0x01E00000; }
SYSCALL int gt4_sys_PSMode() { return 0; }

// The console's system configuration (ConfigParam bitfield): S/PDIF on (0), screen 4:3 (bits 1-2 = 0),
// RGB/component (bit 3 = 0), non-Japanese (bit 4), version 1 (bits 13-15), English (bits 16-20 = 1),
// time zone offset in minutes (bits 21-31, signed) from the host.
SYSCALL void gt4_sys_GetOsdConfigParam(uint32_t *param) {
    TIME_ZONE_INFORMATION tz;
    GetTimeZoneInformation(&tz);
    const int32_t offset = -static_cast<int32_t>(tz.Bias);  // minutes east of UTC
    *param = (1u << 4) | (1u << 13) | (1u << 16) | ((static_cast<uint32_t>(offset) & 0x7FF) << 21);
}
SYSCALL void gt4_sys_SetOsdConfigParam(uint32_t *) {}
SYSCALL void gt4_sys_GetOsdConfigParam2(void *buf, int32_t size, int32_t) {
    if (buf && size > 0) std::memset(buf, 0, static_cast<size_t>(size));
}

SYSCALL void gt4_sys_Exit(int status) {
    log_info("kernel: Exit(%d)", status);
    fflush(stderr);
    ExitProcess(static_cast<UINT>(status));
}

SYSCALL void gt4_sys_unimplemented(int number, uint32_t caller) {
    fatal("kernel: syscall %d (0x%x) not implemented, called from %08x", number, number & 0xFFFF, caller);
}

// ---------------------------------------------------------------------------------------------
// The syscall stubs of the image

extern "C" void gt4_sys_unimplemented_entry();

namespace {

struct SyscallImpl {
    int number;
    const void *fn;
    const char *name;
};

#define S(n, f) {n, reinterpret_cast<const void *>(&gt4_sys_##f), #f}
const SyscallImpl kSyscalls[] = {
    S(0x02, SetGsCrt), S(0x04, Exit),
    S(0x10, AddIntcHandler), S(0x11, RemoveIntcHandler), S(0x12, AddDmacHandler), S(0x13, RemoveDmacHandler),
    S(0x14, EnableIntc), S(0x15, DisableIntc), S(0x16, EnableDmac), S(0x17, DisableDmac),
    S(0x18, SetAlarm), S(0x19, ReleaseAlarm),
    S(-0x1A, EnableIntc), S(-0x1B, DisableIntc), S(-0x1C, EnableDmac), S(-0x1D, DisableDmac),
    S(-0x1E, SetAlarm), S(-0x1F, ReleaseAlarm),
    S(0x20, CreateThread), S(0x21, DeleteThread), S(0x22, StartThread), S(0x23, ExitThread),
    S(0x24, ExitDeleteThread), S(0x25, TerminateThread), S(-0x26, TerminateThread),
    S(0x27, DisableDispatchThread), S(0x28, EnableDispatchThread),
    S(0x29, ChangeThreadPriority), S(-0x2A, ChangeThreadPriority),
    S(0x2B, RotateThreadReadyQueue), S(-0x2C, RotateThreadReadyQueue),
    S(0x2D, ReleaseWaitThread), S(-0x2E, ReleaseWaitThread),
    S(0x2F, GetThreadId), S(0x30, ReferThreadStatus), S(-0x31, ReferThreadStatus),
    S(0x32, SleepThread), S(0x33, WakeupThread), S(-0x34, WakeupThread),
    S(0x35, CancelWakeupThread), S(-0x36, CancelWakeupThread),
    S(0x37, SuspendThread), S(-0x38, SuspendThread), S(0x39, ResumeThread), S(-0x3A, ResumeThread),
    S(0x3C, SetupThread), S(0x3D, SetupHeap), S(0x3E, EndOfHeap),
    S(0x40, CreateSema), S(0x41, DeleteSema), S(0x42, SignalSema), S(-0x43, SignalSema),
    S(0x44, WaitSema), S(0x45, PollSema), S(-0x46, PollSema), S(0x47, ReferSemaStatus),
    S(-0x48, ReferSemaStatus),
    S(0x5C, EnableIntcHandler), S(-0x5C, EnableIntcHandler), S(0x5D, DisableIntcHandler),
    S(-0x5D, DisableIntcHandler), S(0x5E, EnableDmacHandler), S(-0x5E, EnableDmacHandler),
    S(0x5F, DisableDmacHandler), S(-0x5F, DisableDmacHandler),
    S(0x61, EnableCache), S(0x62, DisableCache), S(0x64, FlushCache), S(-0x68, FlushCache),
    S(0x70, GsGetIMR), S(-0x70, GsGetIMR), S(0x71, GsPutIMR), S(-0x71, GsPutIMR),
    S(0x73, SetVSyncFlag), S(0x7D, PSMode), S(0x7E, MachineType), S(0x7F, GetMemorySize),
    S(0x4A, SetOsdConfigParam), S(0x4B, GetOsdConfigParam), S(0x6F, GetOsdConfigParam2),
    // installed by the SDK's InitAlarm (SetSyscall) on the PS2
    S(0xFC, SetAlarm), S(0xFD, ReleaseAlarm), S(-0xFE, SetAlarm), S(-0xFF, ReleaseAlarm),
};
#undef S

}  // namespace

namespace {
struct Stub {
    uint32_t at;
    int number;
};
std::vector<Stub> g_stubs;
}  // namespace

void kernel_scan_syscalls() {
    // Stub pattern: addiu $v1, $zero, N ; syscall ; jr $ra ; nop. Scanned before the trampolines
    // overwrite function entries.
    auto *code = static_cast<const uint32_t *>(guest_ptr(0x00100000));
    const uint32_t words = (0x00617A14 - 0x00100000) / 4;
    for (uint32_t i = 0; i + 1 < words; ++i)
        if (code[i + 1] == 0x0000000C && (code[i] >> 16) == 0x2403)
            g_stubs.push_back({0x00100000 + i * 4, static_cast<int16_t>(code[i] & 0xFFFF)});
}

void kernel_install_syscalls() {
    int implemented = 0, unimplemented = 0;
    for (const Stub &stub : g_stubs) {
        const int number = stub.number;
        const uint32_t at = stub.at;
        const SyscallImpl *impl = nullptr;
        for (const SyscallImpl &s : kSyscalls)
            if (s.number == number) impl = &s;
        auto *p = static_cast<uint8_t *>(guest_ptr(at));
        if (impl) {
            p[0] = 0xE9;  // jmp impl
            const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(impl->fn) - (at + 5));
            std::memcpy(p + 1, &rel, 4);
            ++implemented;
        } else {
            // push N ; jmp gt4_sys_unimplemented_entry  (10 bytes; the stub has 16)
            p[0] = 0x68;
            const int32_t n = number;
            std::memcpy(p + 1, &n, 4);
            p[5] = 0xE9;
            const int32_t rel =
                static_cast<int32_t>(reinterpret_cast<uintptr_t>(&gt4_sys_unimplemented_entry) - (at + 10));
            std::memcpy(p + 6, &rel, 4);
            ++unimplemented;
        }
    }
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    log_info("kernel: %d syscall stubs implemented, %d report themselves", implemented, unimplemented);
}

}  // namespace gt4::rt
