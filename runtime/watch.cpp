// Debugging aid for game-side code (game_src checks): a hardware write watchpoint armed from inside
// the run, for data whose address is only known at run time (a heap object). gt4_debug_watch(addr)
// sets debug register 0 of every thread on the 4 bytes at addr; each write is logged with the value
// left there and the native functions on the writer's stack (frame pointers), up to 64 writes.
#include "image.h"
#include "log.h"

#include <windows.h>
#include <tlhelp32.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

uintptr_t g_watch_addr;
LONG g_watch_hits;

LONG CALLBACK on_watch(EXCEPTION_POINTERS *e) {
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(e->ContextRecord->Dr6 & 1)) return EXCEPTION_CONTINUE_SEARCH;
    e->ContextRecord->Dr6 = 0;
    if (InterlockedIncrement(&g_watch_hits) > 64) {
        e->ContextRecord->Dr7 &= ~1u;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    char line[1024];
    uint32_t guest = 0;
    const char *at = gt4::rt::native_name_for(e->ContextRecord->Eip, &guest);
    int n = snprintf(line, sizeof line, "watch %08x = %08x, written at %s (%08x); callers:", static_cast<unsigned>(g_watch_addr),
                     *reinterpret_cast<volatile uint32_t *>(g_watch_addr), at, guest);
    auto *fp = reinterpret_cast<uintptr_t *>(e->ContextRecord->Ebp);
    for (int i = 0; i < 12 && fp && n < 900; ++i) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery(fp, &mi, sizeof mi) || mi.State != MEM_COMMIT) break;
        const char *name = gt4::rt::native_name_for(fp[1], &guest);
        if (*name) n += snprintf(line + n, sizeof line - n, " %s", name);
        auto *next = reinterpret_cast<uintptr_t *>(fp[0]);
        if (next <= fp) break;
        fp = next;
    }
    gt4::rt::log_info("%s", line);
    return EXCEPTION_CONTINUE_EXECUTION;
}

void arm(HANDLE thread, uintptr_t addr) {
    CONTEXT c;
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(thread, &c)) return;
    c.Dr0 = addr;
    c.Dr7 = (c.Dr7 & ~0xF0003u) | 1u | (1u << 16) | (3u << 18);  // L0, write, 4 bytes
    SetThreadContext(thread, &c);
}

DWORD WINAPI arm_all(void *) {
    const DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te;
    te.dwSize = sizeof te;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
        HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!t) continue;
        SuspendThread(t);
        arm(t, g_watch_addr);
        ResumeThread(t);
        CloseHandle(t);
    }
    CloseHandle(snap);
    return 0;
}

}  // namespace

extern "C" void gt4_debug_watch(void *addr) {
    static bool handler = false;
    if (!handler) {
        AddVectoredExceptionHandler(1, on_watch);
        handler = true;
    }
    g_watch_addr = reinterpret_cast<uintptr_t>(addr);
    g_watch_hits = 0;
    gt4::rt::log_info("watch armed on %p", addr);
    // a helper thread arms every thread, the calling one included (a thread cannot set its own
    // debug registers through SetThreadContext while running)
    HANDLE h = CreateThread(nullptr, 0, arm_all, nullptr, 0, nullptr);
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
}
