/* gt4dbg: runs a 32-bit program under the Win32 debug API and reports every exception with the
 * faulting address, registers and the return addresses found on the stack, symbolized from a GNU ld
 * map file. A small stand-in for a debugger (none ships with the 32-bit MinGW toolchain).
 *
 *   gt4dbg [--map FILE.map] [--first] program.exe [args...]
 *
 * --first: also report first-chance exceptions (by default only the ones the program does not handle).
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

int main(int argc, char **argv) {
    int i = 1, first = 0;
    for (; i < argc && argv[i][0] == '-' && argv[i][1] == '-'; ++i) {
        if (!strcmp(argv[i], "--map") && i + 1 < argc) load_map(argv[++i]);
        else if (!strcmp(argv[i], "--first")) first = 1;
        else if (!strcmp(argv[i], "--watch-call") && i + 1 < argc) watch_addr = (unsigned)strtoul(argv[++i], NULL, 16);
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
        if (!WaitForDebugEvent(&ev, INFINITE)) break;
        DWORD cont = DBG_CONTINUE;
        switch (ev.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            threads[nthreads] = ev.u.CreateProcessInfo.hThread;
            tids[nthreads++] = ev.dwThreadId;
            if (ev.u.CreateProcessInfo.hFile) CloseHandle(ev.u.CreateProcessInfo.hFile);
            break;
        case CREATE_THREAD_DEBUG_EVENT:
            if (nthreads < 4096) threads[nthreads] = ev.u.CreateThread.hThread, tids[nthreads++] = ev.dwThreadId;
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
            if (er->ExceptionCode == EXCEPTION_SINGLE_STEP && watching && wth) {
                watch_step(pi.hProcess, wth);
                cont = DBG_CONTINUE;
                break;
            }
            if (er->ExceptionCode == EXCEPTION_BREAKPOINT || er->ExceptionCode == 0x4000001F) {
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
