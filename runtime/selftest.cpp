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
    log_info("self-test: %s", ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
}
