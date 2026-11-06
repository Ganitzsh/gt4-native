// gt4_game --self-test: checks the kernel's scheduling and interrupt delivery with host functions as
// guest code (guest and host addresses are the same, so a host function's address is a valid entry).
#include "guest.h"
#include "kernel.h"
#include "log.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <string>

using namespace gt4::rt;

extern "C" {
int gt4_sys_CreateThread(void *);
int gt4_sys_StartThread(int, uint32_t);
int gt4_sys_CreateSema(void *);
int gt4_sys_SignalSema(int);
int gt4_sys_WaitSema(int);
int gt4_sys_SleepThread();
int gt4_sys_WakeupThread(int);
int gt4_sys_GetThreadId();
int gt4_sys_ChangeThreadPriority(int, int);
int gt4_sys_RotateThreadReadyQueue(int);
int gt4_sys_SetAlarm(uint32_t, uint32_t, uint32_t);
}

namespace {

struct ThreadParam {
    int32_t status;
    uint32_t func, stack;
    int32_t stack_size;
    uint32_t gp;
    int32_t initial_priority, current_priority;
    uint32_t attr, option;
};
struct SemaParam {
    int32_t count, max_count, init_count, wait_threads;
    uint32_t attr, option;
};

extern "C" int gt4_frame_tick_add(void (*fn)(uint32_t), uint32_t arg, int first);

std::string g_trace;
int g_sema_go = 0, g_sema_done = 0, g_sema_frame = 0;
std::atomic<int> g_frames{0};
volatile int g_spin_flag = 0;

uint32_t addr_of(const void *f) { return guest_addr(f); }

void worker(uint32_t arg) {
    g_trace += "w" + std::to_string(arg) + "start ";
    gt4_sys_WaitSema(g_sema_go);
    g_trace += "w" + std::to_string(arg) + "go ";
    gt4_sys_SignalSema(g_sema_done);
}

void high_prio(uint32_t) {
    g_trace += "hi ";  // must run as soon as it is started (preempts the main thread)
}

void frame_callback(uint32_t) {
    ++g_frames;
    g_spin_flag = 1;
    gt4_sys_SignalSema(g_sema_frame);  // iSignalSema in the frame callbacks' context
}

std::atomic<int> g_alarm_wakes{0};
void alarm_wake(int, uint16_t, uint32_t thread) {   // DelayThread's alarm handler (runtime/iop.cpp)
    ++g_alarm_wakes;
    gt4_sys_WakeupThread(static_cast<int>(thread));
}

int create(void (*fn)(uint32_t), int prio) {
    ThreadParam p{};
    p.func = addr_of(reinterpret_cast<const void *>(fn));
    p.initial_priority = prio;
    return gt4_sys_CreateThread(&p);
}

int sema(int init, int max) {
    SemaParam s{};
    s.init_count = init;
    s.max_count = max;
    return gt4_sys_CreateSema(&s);
}

bool check(bool ok, const char *what) {
    log_info("self-test: %s: %s", what, ok ? "ok" : "FAILED");
    return ok;
}

}  // namespace

int kernel_self_test() {
    bool ok = true;
    g_sema_go = sema(0, 8);
    g_sema_done = sema(0, 8);
    g_sema_frame = sema(0, 1);
    // 1. a higher-priority thread runs at once when started; equal/lower ones wait for the main thread to block
    gt4_sys_ChangeThreadPriority(0, 10);
    const int w1 = create(worker, 20), w2 = create(worker, 20), hi = create(high_prio, 5);
    gt4_sys_StartThread(w1, 1);
    gt4_sys_StartThread(w2, 2);
    g_trace += "main ";
    gt4_sys_StartThread(hi, 0);
    g_trace += "main2 ";
    ok &= check(g_trace == "main hi main2 ", ("preemption order [" + g_trace + "]").c_str());
    // 2. blocking hands the CPU to ready threads in FIFO order; w1 finds the semaphore signalled and
    //    runs through before w2 starts (equal priority: no time slicing)
    g_trace.clear();
    gt4_sys_SignalSema(g_sema_go);
    gt4_sys_SignalSema(g_sema_go);
    gt4_sys_WaitSema(g_sema_done);
    gt4_sys_WaitSema(g_sema_done);
    ok &= check(g_trace == "w1start w1go w2start w2go ", ("semaphores [" + g_trace + "]").c_str());
    // 3. the frame clock calls a frame callback, which wakes a waiting thread
    gt4_frame_tick_add(frame_callback, 0, 0);
    for (int i = 0; i < 30; ++i) gt4_sys_WaitSema(g_sema_frame);
    ok &= check(g_frames >= 30, ("30 frames waited (" + std::to_string(g_frames.load()) + ")").c_str());
    // 4. a frame callback reaches a thread spinning in guest code (it is frozen, the callback runs, it resumes)
    g_spin_flag = 0;
    const DWORD t0 = GetTickCount();
    while (!g_spin_flag && GetTickCount() - t0 < 1000) {
    }
    ok &= check(g_spin_flag == 1, "interrupt delivered to a spinning thread");
    // 5. DelayThread's alarm comes due before the thread sleeps: the handler runs on the thread itself at its next
    //    safe point (here the end of a KernelScope) and its wakeup must still count for the SleepThread after it.
    //    A second alarm 1 s later wakes the thread if that wakeup was lost (the Odin's race freeze).
    const uint32_t me = static_cast<uint32_t>(gt4_sys_GetThreadId());
    const uint32_t handler = addr_of(reinterpret_cast<const void *>(&alarm_wake));
    g_alarm_wakes = 0;
    gt4_sys_SetAlarm(15, handler, me);        // 1 ms
    gt4_sys_SetAlarm(15734, handler, me);     // 1 s: the rescue
    {
        KernelScope ks;
        Sleep(5);                             // the thread descheduled past the alarm (the Odin under load)
    }
    const DWORD t1 = GetTickCount();
    gt4_sys_SleepThread();
    const DWORD slept = GetTickCount() - t1;
    ok &= check(slept < 500, ("an alarm due before the sleep still wakes the thread (slept " + std::to_string(slept) + " ms)").c_str());
    if (slept < 500) {                        // (otherwise the rescue is what woke it)
        while (g_alarm_wakes < 2) Sleep(1);   // let the rescue alarm fire
        gt4_sys_SleepThread();                // and take its wakeup (counted while this thread ran)
    }
    log_info("self-test: %s", ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
}
