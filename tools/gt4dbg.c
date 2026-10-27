/* gt4dbg: runs a 32-bit program under the Win32 debug API and reports every exception with the
 * faulting address, registers and the return addresses found on the stack, symbolized from a GNU ld
 * map file. A small stand-in for a debugger (none ships with the 32-bit MinGW toolchain).
 *
 *   gt4dbg [--map FILE.map] [--first] program.exe [args...]
 *
 * --first: also report first-chance exceptions (by default only the ones the program does not handle).
 * --watch-write ADDR [--watch-value V] [--watch-after SECONDS]: a hardware data breakpoint on the 4 bytes at ADDR in every
 *   thread (only writes leaving V there are reported, with --watch-value); each write
 *   is reported with the writing instruction and the native functions on the stack (at most 40).
 * Build: C:\msys64\mingw32\bin\gcc -O1 tools/gt4dbg.c -o build/gt4dbg.exe
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { unsigned addr; char name[96]; } Sym;
static Sym *syms;
static int nsyms;

static int cmp_sym(const void *a, const void *b) {
    unsigned x = ((const Sym *)a)->addr, y = ((const Sym *)b)->addr;
    return x < y ? -1 : x > y;
}

static void load_map(const char *path) {
    FILE *f = fopen(path, "r");
    char line[1024];
    int cap = 1 << 16;
    if (!f) return;
    syms = malloc(sizeof(Sym) * cap);
    while (fgets(line, sizeof line, f)) {
        unsigned a;
        char name[512], type;
        /* map: "                0x02001234                _func_00107F08"; nm: "02001234 T _func_00107F08" */
        int ok = sscanf(line, " 0x%x %511s", &a, name) == 2 && name[0] != '0' && name[0] != '=' && !strchr(line, '=');
        if (!ok) ok = sscanf(line, "%x %c %511s", &a, &type, name) == 3 && (type == 'T' || type == 't');
        if (ok && a >= 0x02000000 && a < 0x08000000) {
            if (nsyms == cap) syms = realloc(syms, sizeof(Sym) * (cap *= 2));
            syms[nsyms].addr = a;
            snprintf(syms[nsyms].name, sizeof syms[nsyms].name, "%s", name);
            ++nsyms;
        }
    }
    fclose(f);
    qsort(syms, nsyms, sizeof(Sym), cmp_sym);
}

static const char *sym_for(unsigned a, unsigned *off) {
    int lo = 0, hi = nsyms - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (syms[mid].addr <= a) best = mid, lo = mid + 1;
        else hi = mid - 1;
    }
    if (best < 0) return NULL;
    *off = a - syms[best].addr;
    return syms[best].name;
}

static void show(unsigned a) {
    unsigned off;
    const char *n = sym_for(a, &off);
    if (a < 0x02000000) printf("%08x (PS2 address / trampoline)", a);
    else if (n) printf("%08x %s+0x%x", a, n, off);
    else printf("%08x", a);
}

/* --watch-call ADDR: when the instruction at host address ADDR (a call) executes, single-step until it
 * returns, watching every stack slot where a callee saves ebx/esi/edi/ebp (push); report the first
 * instruction that overwrites such a slot before it is popped (stack corruption). */
typedef struct { unsigned slot, value, pushed_at; } Saved;
static Saved saved[4096];
static int nsaved;
static unsigned watch_addr, watch_ret_esp;
static unsigned char watch_orig;
static int watching, watch_steps;

static void set_tf(HANDLE th) {
    CONTEXT c;
    c.ContextFlags = CONTEXT_CONTROL;
    GetThreadContext(th, &c);
    c.EFlags |= 0x100;
    SetThreadContext(th, &c);
}

static void watch_step(HANDLE proc, HANDLE th) {
    CONTEXT c;
    c.ContextFlags = CONTEXT_FULL;
    GetThreadContext(th, &c);
    ++watch_steps;
    /* slots above esp were popped */
    while (nsaved && saved[nsaved - 1].slot < c.Esp) --nsaved;
    for (int k = 0; k < nsaved; ++k) {
        unsigned v = 0;
        SIZE_T got;
        ReadProcessMemory(proc, (void *)(uintptr_t)saved[k].slot, &v, 4, &got);
        if (v != saved[k].value) {
            printf("STACK CORRUPTION: saved register slot %08x (pushed at ", saved[k].slot);
            show(saved[k].pushed_at);
            printf(") changed %08x -> %08x; now at ", saved[k].value, v);
            show(c.Eip);
            printf(" after %d steps\n", watch_steps);
            fflush(stdout);
            watching = 0;
            return;
        }
    }
    unsigned char op = 0;
    SIZE_T got;
    ReadProcessMemory(proc, (void *)(uintptr_t)c.Eip, &op, 1, &got);
    static int pending_push;
    static unsigned pending_at;
    if (pending_push) {  /* the push executed: the slot is the new esp */
        unsigned v = 0;
        ReadProcessMemory(proc, (void *)(uintptr_t)c.Esp, &v, 4, &got);
        if (nsaved < 4096) saved[nsaved++] = (Saved){c.Esp, v, pending_at};
        pending_push = 0;
    }
    if (op == 0x53 || op == 0x55 || op == 0x56 || op == 0x57) { /* push ebx/ebp/esi/edi */
        pending_push = 1;
        pending_at = c.Eip;
    }
    if (c.Esp > watch_ret_esp) { /* returned from the watched call */
        printf("watch: the call returned after %d steps with no corruption\n", watch_steps);
        watching = 0;
        return;
    }
    if (watch_steps > 50000000) {
        printf("watch: gave up after %d steps\n", watch_steps);
        watching = 0;
        return;
    }
    set_tf(th);
}

/* --watch-write ADDR: DR0 on ADDR, write, 4 bytes (DR7: L0, RW0 = 01, LEN0 = 11) */
static unsigned write_addr, write_dr7 = 0x000D0001;
static int write_hits;

/* --scan WORD --scan-after SECONDS: once, after that many seconds, list the guest RAM addresses
 * (00011000-02000000) holding the 32-bit WORD */
static unsigned scan_word, scan_after;
static int scan_back = -4;

/* --dump ADDR LEN (with --scan-after): show LEN bytes at ADDR instead of scanning */
static unsigned dump_addr, dump_len;

static void scan_memory(HANDLE proc) {
    static unsigned char buf[1 << 20];
    int found = 0;
    if (dump_addr) {
        SIZE_T got = 0;
        ReadProcessMemory(proc, (void *)(uintptr_t)dump_addr, buf, dump_len < sizeof buf ? dump_len : sizeof buf, &got);
        for (SIZE_T k = 0; k < got; k += 16) {
            printf("dump %08x:", (unsigned)(dump_addr + k));
            for (SIZE_T j = k; j < k + 16 && j + 4 <= got; j += 4) printf(" %08x", *(unsigned *)(buf + j));
            printf("\n");
        }
        fflush(stdout);
        return;
    }
    for (unsigned base = 0x00010000; base < 0x02000000 && found < 64; base += sizeof buf) {
        SIZE_T got = 0;
        if (!ReadProcessMemory(proc, (void *)(uintptr_t)base, buf, sizeof buf, &got)) continue;
        for (SIZE_T k = 0; k + 4 <= got && found < 64; k += 4)
            if (*(unsigned *)(buf + k) == scan_word) {
                printf("scan: %08x at %08x:", scan_word, (unsigned)(base + k));
                for (int j = scan_back; j < 8; ++j)
                    if (k + 4 * j < got) printf(" %08x", *(unsigned *)(buf + k + 4 * j));
                printf("\n");
                ++found;
            }
    }
    printf("scan: %d found\n", found);
    fflush(stdout);
}

static unsigned write_value;
static int write_has_value;
static unsigned watch_after, start_ticks; /* --watch-after SECONDS: ignore the writes before then */

static void arm_write(HANDLE th) {
    CONTEXT c;
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(th, &c)) return;
    c.Dr0 = write_addr;
    c.Dr7 = write_dr7;
    SetThreadContext(th, &c);
}

static void report_write(HANDLE proc, HANDLE th, DWORD tid) {
    CONTEXT c;
    c.ContextFlags = CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS;
    GetThreadContext(th, &c);
    unsigned v = 0;
    SIZE_T got;
    ReadProcessMemory(proc, (void *)(uintptr_t)write_addr, &v, 4, &got);
    if ((write_has_value && v != write_value) || GetTickCount() - start_ticks < watch_after * 1000u) {
        /* --watch-value: only that value is reported; --watch-after: nothing before that time */
        c.Dr6 = 0;
        c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        SetThreadContext(th, &c);
        return;
    }
    printf("write #%d to %08x = %08x by thread %lu at ", ++write_hits, write_addr, v, tid);
    show(c.Eip);
    printf("\n  eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx ebp %08lx esp %08lx\n", c.Eax, c.Ebx,
           c.Ecx, c.Edx, c.Esi, c.Edi, c.Ebp, c.Esp);
    unsigned stack[1024];
    got = 0;
    ReadProcessMemory(proc, (void *)(uintptr_t)c.Esp, stack, sizeof stack, &got);
    for (unsigned k = 0, shown = 0; k < got / 4 && shown < 10; ++k) {
        unsigned off;
        if (stack[k] >= 0x02000000 && stack[k] < 0x08000000 && sym_for(stack[k], &off)) {
            printf("  [esp+%04x] ", k * 4);
            show(stack[k]);
            printf("\n");
            ++shown;
        }
    }
    fflush(stdout);
    c.Dr6 = 0;
    if (write_hits >= 40) c.Dr7 = 0;
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    SetThreadContext(th, &c);
}

/* --log-at ADDR (up to 8, repeatable): a breakpoint that logs the registers and the top of the stack
 * each time the instruction at ADDR runs (at most 400 times each), then lets it run on. */
static unsigned log_at[8];
static unsigned char log_orig[8];
static int log_count[8], nlog;
static unsigned log_rearm[4096]; /* per thread: the breakpoint to re-plant after the single step */

static void plant(HANDLE proc, unsigned addr, unsigned char *orig) {
    SIZE_T got;
    unsigned char cc = 0xCC;
    if (orig) ReadProcessMemory(proc, (void *)(uintptr_t)addr, orig, 1, &got);
    WriteProcessMemory(proc, (void *)(uintptr_t)addr, &cc, 1, &got);
    FlushInstructionCache(proc, NULL, 0);
}

static int log_hit(HANDLE proc, HANDLE th, int tindex, DWORD tid, unsigned addr) {
    int k;
    for (k = 0; k < nlog; ++k)
        if (log_at[k] == addr) break;
    if (k == nlog) return 0;
    CONTEXT c;
    c.ContextFlags = CONTEXT_FULL;
    GetThreadContext(th, &c);
    unsigned st[8] = {0};
    SIZE_T got;
    ReadProcessMemory(proc, (void *)(uintptr_t)c.Esp, st, sizeof st, &got);
    printf("at %08x #%d thread %lu: eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx ebp %08lx esp %08lx"
           " | %08x %08x %08x %08x %08x %08x\n", addr, ++log_count[k], tid, c.Eax, c.Ebx, c.Ecx, c.Edx, c.Esi, c.Edi,
           c.Ebp, c.Esp, st[0], st[1], st[2], st[3], st[4], st[5]);
    fflush(stdout);
    SIZE_T w;
    WriteProcessMemory(proc, (void *)(uintptr_t)addr, &log_orig[k], 1, &w);
    FlushInstructionCache(proc, NULL, 0);
    c.Eip = addr;
    if (log_count[k] < 400) {
        c.EFlags |= 0x100;
        log_rearm[tindex] = addr;
    }
    c.ContextFlags = CONTEXT_CONTROL;
    SetThreadContext(th, &c);
    return 1;
}

int main(int argc, char **argv) {
    int i = 1, first = 0;
    start_ticks = GetTickCount();
    for (; i < argc && argv[i][0] == '-' && argv[i][1] == '-'; ++i) {
        if (!strcmp(argv[i], "--map") && i + 1 < argc) load_map(argv[++i]);
        else if (!strcmp(argv[i], "--first")) first = 1;
        else if (!strcmp(argv[i], "--watch-call") && i + 1 < argc) watch_addr = (unsigned)strtoul(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "--watch-write") && i + 1 < argc) write_addr = (unsigned)strtoul(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "--watch-rw") && i + 1 < argc) write_addr = (unsigned)strtoul(argv[++i], NULL, 16), write_dr7 = 0x000F0001;
        else if (!strcmp(argv[i], "--watch-after") && i + 1 < argc) watch_after = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--watch-value") && i + 1 < argc) write_value = (unsigned)strtoul(argv[++i], NULL, 16), write_has_value = 1;
        else if (!strcmp(argv[i], "--log-at") && i + 1 < argc && nlog < 8) log_at[nlog++] = (unsigned)strtoul(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "--scan") && i + 1 < argc) scan_word = (unsigned)strtoul(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "--dump") && i + 2 < argc)
            dump_addr = (unsigned)strtoul(argv[i + 1], NULL, 16), dump_len = (unsigned)strtoul(argv[i + 2], NULL, 0), i += 2;
        else if (!strcmp(argv[i], "--scan-after") && i + 1 < argc) scan_after = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scan-back") && i + 1 < argc) scan_back = -atoi(argv[++i]);
    }
    if (i >= argc) {
        fprintf(stderr, "usage: gt4dbg [--map FILE] [--first] program [args]\n");
        return 2;
    }
    char cmd[8192] = "";
    for (int j = i; j < argc; ++j) {
        strcat(cmd, "\"");
        strcat(cmd, argv[j]);
        strcat(cmd, "\" ");
    }
    STARTUPINFOA si = {sizeof si};
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, DEBUG_ONLY_THIS_PROCESS, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "cannot start %s (error %lu)\n", argv[i], GetLastError());
        return 2;
    }
    DEBUG_EVENT ev;
    DWORD exit_code = 0;
    HANDLE threads[4096] = {0};
    DWORD tids[4096] = {0};
    int nthreads = 0;
    for (;;) {
        if (!WaitForDebugEvent(&ev, scan_after ? 500 : INFINITE)) {
            if (GetLastError() != ERROR_SEM_TIMEOUT) break;
            static DWORD start;
            if (!start) start = GetTickCount();
            if (GetTickCount() - start >= scan_after * 1000) {
                DebugBreakProcess(pi.hProcess);  /* stop it while the memory is read */
                scan_memory(pi.hProcess);
                scan_after = 0;
            }
            continue;
        }
        DWORD cont = DBG_CONTINUE;
        switch (ev.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            threads[nthreads] = ev.u.CreateProcessInfo.hThread;
            tids[nthreads++] = ev.dwThreadId;
            if (write_addr) arm_write(ev.u.CreateProcessInfo.hThread);
            if (ev.u.CreateProcessInfo.hFile) CloseHandle(ev.u.CreateProcessInfo.hFile);
            break;
        case CREATE_THREAD_DEBUG_EVENT:
            if (nthreads < 4096) threads[nthreads] = ev.u.CreateThread.hThread, tids[nthreads++] = ev.dwThreadId;
            if (write_addr && write_hits < 40) arm_write(ev.u.CreateThread.hThread);
            break;
        case LOAD_DLL_DEBUG_EVENT:
            if (ev.u.LoadDll.hFile) CloseHandle(ev.u.LoadDll.hFile);
            break;
        case OUTPUT_DEBUG_STRING_EVENT:
            break;
        case EXCEPTION_DEBUG_EVENT: {
            EXCEPTION_RECORD *er = &ev.u.Exception.ExceptionRecord;
            int is_first = ev.u.Exception.dwFirstChance;
            cont = DBG_EXCEPTION_NOT_HANDLED;
            HANDLE wth = NULL;
            for (int k = 0; k < nthreads; ++k) if (tids[k] == ev.dwThreadId) wth = threads[k];
            if (er->ExceptionCode == EXCEPTION_BREAKPOINT && watch_addr &&
                (unsigned)(uintptr_t)er->ExceptionAddress == watch_addr && wth) {
                /* our breakpoint: put the byte back, rewind eip, start stepping */
                SIZE_T w;
                WriteProcessMemory(pi.hProcess, (void *)(uintptr_t)watch_addr, &watch_orig, 1, &w);
                FlushInstructionCache(pi.hProcess, NULL, 0);
                CONTEXT c;
                c.ContextFlags = CONTEXT_CONTROL;
                GetThreadContext(wth, &c);
                c.Eip = watch_addr;
                c.EFlags |= 0x100;
                SetThreadContext(wth, &c);
                watch_ret_esp = c.Esp;  /* back above this once the call returns */
                watching = 1;
                nsaved = 0;
                printf("watch: call at %08x reached, stepping\n", watch_addr);
                cont = DBG_CONTINUE;
                break;
            }
            int tindex = 0;
            for (int k = 0; k < nthreads; ++k) if (tids[k] == ev.dwThreadId) tindex = k;
            if (er->ExceptionCode == EXCEPTION_BREAKPOINT && nlog && wth &&
                log_hit(pi.hProcess, wth, tindex, ev.dwThreadId, (unsigned)(uintptr_t)er->ExceptionAddress)) {
                cont = DBG_CONTINUE;
                break;
            }
            if (er->ExceptionCode == EXCEPTION_SINGLE_STEP && log_rearm[tindex]) {
                for (int k = 0; k < nlog; ++k)
                    if (log_at[k] == log_rearm[tindex]) plant(pi.hProcess, log_at[k], NULL);
                log_rearm[tindex] = 0;
                cont = DBG_CONTINUE;
                break;
            }
            if (er->ExceptionCode == EXCEPTION_SINGLE_STEP && write_addr && wth && !watching) {
                report_write(pi.hProcess, wth, ev.dwThreadId);
                cont = DBG_CONTINUE;
                break;
            }
            if (er->ExceptionCode == EXCEPTION_SINGLE_STEP && watching && wth) {
                watch_step(pi.hProcess, wth);
                cont = DBG_CONTINUE;
                break;
            }
            if (er->ExceptionCode == EXCEPTION_BREAKPOINT || er->ExceptionCode == 0x4000001F) {
                static int planted;
                if (nlog && !planted) {  /* the loader breakpoint: plant the logging breakpoints */
                    planted = 1;
                    for (int k = 0; k < nlog; ++k) plant(pi.hProcess, log_at[k], &log_orig[k]);
                }
                if (watch_addr && !watch_orig) {
                    /* the loader breakpoint: plant ours now */
                    SIZE_T got;
                    unsigned char cc = 0xCC;
                    ReadProcessMemory(pi.hProcess, (void *)(uintptr_t)watch_addr, &watch_orig, 1, &got);
                    WriteProcessMemory(pi.hProcess, (void *)(uintptr_t)watch_addr, &cc, 1, &got);
                    FlushInstructionCache(pi.hProcess, NULL, 0);
                }
                cont = DBG_CONTINUE;
                break;
            }
            if (er->ExceptionCode == 0x406D1388) break; /* thread naming */
            if (is_first && !first) break;
            HANDLE th = NULL;
            for (int k = 0; k < nthreads; ++k) if (tids[k] == ev.dwThreadId) th = threads[k];
            CONTEXT c;
            c.ContextFlags = CONTEXT_FULL;
            printf("\n=== %s exception %08lx in thread %lu at ", is_first ? "first-chance" : "UNHANDLED",
                   er->ExceptionCode, ev.dwThreadId);
            show((unsigned)(uintptr_t)er->ExceptionAddress);
            if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
                printf(" (%s %08lx)", er->ExceptionInformation[0] ? "write" : "read",
                       (unsigned long)er->ExceptionInformation[1]);
            printf("\n");
            if (th && GetThreadContext(th, &c)) {
                printf("  eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx ebp %08lx esp %08lx\n",
                       c.Eax, c.Ebx, c.Ecx, c.Edx, c.Esi, c.Edi, c.Ebp, c.Esp);
                unsigned stack[2048];
                SIZE_T got = 0;
                ReadProcessMemory(pi.hProcess, (void *)(uintptr_t)c.Esp, stack, sizeof stack, &got);
                int shown = 0;
                for (unsigned k = 0; k < got / 4 && shown < 30; ++k) {
                    unsigned v = stack[k], off;
                    if (v >= 0x02000000 && v < 0x08000000 && sym_for(v, &off)) {
                        printf("  [esp+%04x] ", k * 4);
                        show(v);
                        printf("\n");
                        ++shown;
                    }
                }
            }
            fflush(stdout);
            break;
        }
        case EXIT_PROCESS_DEBUG_EVENT:
            exit_code = ev.u.ExitProcess.dwExitCode;
            printf("process exited with %08lx\n", exit_code);
            ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, DBG_CONTINUE);
            return (int)exit_code;
        default:
            break;
        }
        ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, cont);
    }
    return (int)exit_code;
}
